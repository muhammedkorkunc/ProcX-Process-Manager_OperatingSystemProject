/*
 * ======================================================================================
 * Dosya Adı    : procx.c
 * Proje Adı    : İşletim Sistemleri Dönem Projesi 
 * Yazar        : Muhammed Emin Korkunç
 * Öğrenci No   : 2021221054
 * Tarih        : 18 Aralık 2024
 *
 * Açıklama:
 * Bu program, Linux ortamında çalışan, çoklu işlem (multiprocessing) yönetimini sağlayan
 * bir "Süreç Yönetim Sistemi"dir. Temel özellikleri şunlardır:
 * 1. Process Yönetimi: fork() ve exec() ile yeni süreçler oluşturur.
 * 2. IPC (Inter-Process Communication): Shared Memory, Semaphore ve Message Queue kullanarak
 * süreçler arası haberleşmeyi ve veri paylaşımını sağlar.
 * 3. Thread Yapısı: Monitor ve Listener threadleri ile arka plan işlerini yürütür.
 * 4. Attached/Detached Mod: Süreçlerin terminale bağımlılığını yönetir.
 * 5. Güvenlik: Race Condition, Zombie Process ve Buffer Overflow önlemleri alınmıştır.
 * ======================================================================================
 */

// POSIX standartlarını ve System V IPC özelliklerini etkinleştiriyoruz.
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <stdio.h>      // Standart giriş/çıkış (printf, scanf)
#include <stdlib.h>     // Standart kütüphane (exit, atoi, malloc)
#include <unistd.h>     // POSIX API (fork, exec, getpid, sleep)
#include <string.h>     // String işlemleri (strcpy, strlen)
#include <signal.h>     // Sinyal yönetimi (kill, sigaction)
#include <pthread.h>    // Thread (İş parçacığı) işlemleri
#include <sys/types.h>  // Sistem veri tipleri (pid_t)
#include <sys/wait.h>   // Süreç bekleme (waitpid)
#include <sys/mman.h>   // Memory mapping (mmap - Shared Memory için)
#include <fcntl.h>      // Dosya kontrol (open, O_CREAT)
#include <semaphore.h>  // Semafor işlemleri
#include <errno.h>      // Hata kodları
#include <time.h>       // Zaman fonksiyonları
#include <sys/ipc.h>    // System V IPC genel başlık
#include <sys/msg.h>    // System V Message Queue
#include <ctype.h>      // Karakter tipi kontrolleri

/* --- SABİT AYARLAR VE TANIMLAMALAR --- */

#define MAX_PROCESSES 50            // Sistemde takip edilecek maksimum süreç sayısı
#define SHM_NAME "/procx_shm_area"  // Paylaşılan bellek (Shared Memory) dosya adı
#define SEM_NAME "/procx_sem_lock"  // Semafor (Kilit) adı
#define MSG_KEY_FILE "/tmp/procx_msgkey" // Mesaj kuyruğu anahtarı için kullanılacak dosya
#define MSG_KEY_ID   65             // ftok fonksiyonu için proje ID'si
#define PROXC_MAGIC 0x50524F58      // Belleğin ilk kez oluşturulduğunu anlamak için sihirli sayı

/* --- KOMUT TİPLERİ (IPC İÇİN) --- */
#define CMD_START 1      // Yeni süreç başlatıldı bildirimi
#define CMD_TERMINATE 2  // Süreç sonlandırıldı bildirimi

/* --- VERİ YAPILARI --- */

/* Sürecin çalışma modunu belirten enum (Bağlı veya Ayrık) */
typedef enum { ATTACHED = 0, DETACHED = 1 } ProcessMode;

/* Sürecin durumunu belirten enum (Çalışıyor veya Sonlandı) */
typedef enum { STATUS_RUNNING = 0, STATUS_TERMINATED = 1 } ProcessStatus;

/* Process Control Block (PCB) - Her bir sürecin bilgisini tutan yapı */
typedef struct {
    pid_t pid;              // Sürecin ID'si (Process ID)
    pid_t owner_pid;        // Bu süreci başlatan ProcX örneğinin PID'si
    char command[256];      // Çalıştırılan komutun adı (örn: "sleep 100")
    ProcessMode mode;       // Attached (0) veya Detached (1)
    ProcessStatus status;   // Running (0) veya Terminated (1)
    time_t start_time;      // Sürecin başlatıldığı zaman damgası
    int is_active;          // Bu slot dolu mu? (1: Evet, 0: Hayır)
} ProcessInfo;

/* Paylaşılan Bellek (Shared Memory) Yapısı
 * Tüm ProcX örnekleri bu yapıyı ortak kullanır.
 */
typedef struct {
    ProcessInfo processes[MAX_PROCESSES]; // Süreç listesi dizisi
    int process_count;                    // Toplam aktif süreç sayısı
    int magic;                            // Başlatma kontrolü (Init check)
    int instance_count;                   // Açık olan ProcX terminal sayısı
} SharedData;

/* Mesaj Kuyruğu (Message Queue) Yapısı */
typedef struct {
    long msg_type;      // Mesaj tipi (System V gereği > 0 olmalı)
    int command;        // Komut tipi (CMD_START veya CMD_TERMINATE)
    pid_t sender_pid;   // Mesajı gönderen ProcX'in PID'si
    pid_t target_pid;   // İşlem yapılan hedef sürecin PID'si
} Message;

/* --- GLOBAL DEĞİŞKENLER --- */
static SharedData *shared_data = NULL; // Paylaşılan belleğe erişim pointer'ı
static sem_t *shm_sem = NULL;          // Semafor kilidi
static int shm_fd = -1;                // Paylaşılan bellek dosya tanımlayıcısı
static int msgid = -1;                 // Mesaj kuyruğu ID'si
static volatile sig_atomic_t running = 1; // Programın ana döngü kontrolü
static pthread_t monitor_thread;       // Monitor Thread ID
static pthread_t listener_thread;      // IPC Listener Thread ID

/* --- YARDIMCI FONKSİYONLAR --- */

/* Buffer taşmasını önleyen güvenli string kopyalama fonksiyonu */
static void safe_strcpy(char *dst, const char *src, size_t dstsz) {
    if (!dst || dstsz == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    strncpy(dst, src, dstsz - 1);
    dst[dstsz - 1] = '\0'; // Son karakteri mutlaka null yap
}

/* Ctrl+C (SIGINT) sinyal yakalayıcısı */
static void sigint_handler(int signum) {
    (void)signum;  
    running = 0; // Döngüyü kırarak programın güvenli çıkış yapmasını sağlar
}

/* Giriş tamponunu (buffer) temizler (Hatalı girişlerde döngüyü engeller) */
void clear_input_buffer() {
    int c; while ((c = getchar()) != '\n' && c != EOF);
}

/* Kullanıcıdan güvenli bir şekilde tamsayı alan fonksiyon.
 * Harf girilmesini ve belirlenen aralık dışına çıkılmasını engeller.
 */
int get_safe_int(const char* prompt, int min, int max) {
    char buffer[64];
    int value;
    char *endptr;
    while (1) {
        printf("%s", prompt); // İstenen mesajı bas (örn: "Seçiniz: ")
        if (fgets(buffer, sizeof(buffer), stdin) == NULL) return -1; // EOF kontrolü
        if (buffer[0] == '\n') continue; // Boş satırsa tekrar sor

        errno = 0;
        value = strtol(buffer, &endptr, 10); // String'i Long Int'e çevir

        // Dönüşüm hatasızsa ve değer aralıktaysa döndür
        if (errno == 0 && endptr != buffer && (*endptr == '\n' || *endptr == '\0') && 
            value >= min && value <= max) return value;
        
        printf("  [HATA] Geçersiz giriş! (%d-%d arası bir sayı giriniz)\n", min, max);
    }
}

/* Kullanıcıdan güvenli bir şekilde string (metin) alan fonksiyon 
fgets ile hafıza taşmasını önledim
*/
int get_safe_string(char *buffer, int size) {
    if (fgets(buffer, size, stdin) == NULL) return 0;
    size_t len = strlen(buffer);
    // Sondaki yeni satır (\n) karakterini temizle
    if (len > 0 && buffer[len-1] == '\n') {
        buffer[len-1] = '\0';
        return 1;
    }
    clear_input_buffer(); // Tamponda kalan fazlalıkları temizle
    return 1;
}

/* Ana menüyü ekrana yazdıran fonksiyon */
void show_menu() {
    printf("\n");
    printf("╔════════════════════════════════════╗\n");
    printf("║ ProcX                              ║\n");
    printf("╠════════════════════════════════════╣\n");
    printf("║ 1. Yeni Program Çalıştır           ║\n");
    printf("║ 2. Çalışan Programları Listele     ║\n");
    printf("║ 3. Program Sonlandır               ║\n");
    printf("║ 0. Çıkış                           ║\n");
    printf("╚════════════════════════════════════╝\n");
}

/* IPC Mesajı Gönderen Fonksiyon (Message Queue) */
static void send_message(int cmd, pid_t target) {
    if (msgid == -1) return;
    Message msg;
    msg.msg_type = 1;         // Mesaj tipi her zaman 1
    msg.command = cmd;        // Komut (Başlat/Bitir)
    msg.sender_pid = getpid();// Gönderen biziz
    msg.target_pid = target;  // Hedef süreç ID'si
    
    // msgsnd ile kuyruğa yaz (Hata olursa devam et)
    if (msgsnd(msgid, &msg, sizeof(Message) - sizeof(long), 0) == -1) { }
}

/* --- [ÖZEL FONKSİYON] SANITIZER: Ölü Süreç Temizleyici --- */
/* Program başladığında hafızada kalmış ama gerçekte ölmüş (hayalet) süreçleri temizler.
 * Bu sayede "Attached" süreçler program kapanıp açılınca listede kalmaz.
 */
static void sanitize_shared_memory() {
    sem_wait(shm_sem); // Veri bütünlüğü için kilitle
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (shared_data->processes[i].is_active) {
            // kill(pid, 0) sinyal göndermez, sadece process var mı diye bakar.
            if (kill(shared_data->processes[i].pid, 0) == -1 && errno == ESRCH) {
                // Process sistemde yok ama listede var -> TEMİZLE
                shared_data->processes[i].is_active = 0;
                if (shared_data->process_count > 0) shared_data->process_count--;
            }
        }
    }
    sem_post(shm_sem); // Kilidi aç
}

/* --- [ÖZEL FONKSİYON] FLUSH: Mesaj Kuyruğu Temizleyici --- */
/* Program başladığında kuyrukta kalmış eski mesajları temizler.
 * Böylece program açılır açılmaz eski bildirimler ekrana düşmez.
 */
static void flush_message_queue() {
    Message msg;
    // IPC_NOWAIT ile bloklanmadan, kuyruk boşalana kadar oku ve yut.
    while (msgrcv(msgid, &msg, sizeof(Message) - sizeof(long), 0, IPC_NOWAIT) > 0) {
        // Okundu ve silindi.
    }
}

/* --- SİSTEM BAŞLATMA (INIT) --- */
static void init_ipc(void) {
    // 1. Shared Memory Oluşturma/Açma (POSIX shm_open)
    shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    ftruncate(shm_fd, sizeof(SharedData)); // Boyut ayarla
    // Belleği map et (mmap)
    shared_data = (SharedData*)mmap(NULL, sizeof(SharedData), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

    // 2. Semafor Oluşturma/Açma (Named Semaphore)
    shm_sem = sem_open(SEM_NAME, O_CREAT, 0666, 1);

    // 3. İlk Başlatma Kontrolü (Magic Number)
    sem_wait(shm_sem);
    if (shared_data->magic != PROXC_MAGIC) {
        // Bellek ilk kez oluşturuluyorsa sıfırla
        memset(shared_data, 0, sizeof(SharedData));
        shared_data->magic = PROXC_MAGIC;
    }
    shared_data->instance_count++; // Aktif terminal sayısını artır
    sem_post(shm_sem);

    // 4. Message Queue Oluşturma/Açma (System V)
    FILE *fp = fopen(MSG_KEY_FILE, "a"); // ftok için dosya oluştur
    if (fp) fclose(fp);
    key_t key = ftok(MSG_KEY_FILE, MSG_KEY_ID); // Benzersiz anahtar üret
    msgid = msgget(key, 0666 | IPC_CREAT); // Kuyruğu al
    
    // Eğer ilk açılan terminal bizsek, eski mesajları temizle
    if (shared_data->instance_count == 1) {
        flush_message_queue();
    }
    // Ölü süreçleri temizle
    sanitize_shared_memory();
}

/* --- 6.1 PROCESS BAŞLATMA --- 
start_new_program fonksiyonu fork ve execvp kullanıyor. 
Attached/Detached ayrımı var. setsid() kullanılıyor. 
IPC mesajı gönderiliyor
*/
static void start_new_program(void) {
    char command[256];
    char *args[20];
    int i = 0, mode = 0;

    // Kullanıcıdan komut al
    printf("Çalıştırılacak komutu girin: ");
    get_safe_string(command, sizeof(command));
    if (strlen(command) == 0) { printf("  [HATA] Boş komut!\n"); return; }

    // Mod seçimi (0 veya 1)
    mode = get_safe_int("Mod seçin (0: Attached, 1: Detached): ", 0, 1);
    if (mode == -1) return;

    // Komutu argümanlara böl (parse)
    char temp[256]; safe_strcpy(temp, command, sizeof(temp));
    char *token = strtok(temp, " ");
    while (token != NULL && i < 19) { args[i++] = token; token = strtok(NULL, " "); }
    args[i] = NULL;

    if (args[0] == NULL) return;

    // Maksimum süreç kontrolü
    sem_wait(shm_sem);
    if (shared_data->process_count >= MAX_PROCESSES) {
        sem_post(shm_sem);
        printf("  [HATA] Limit dolu!\n");
        return;
    }
    sem_post(shm_sem);

    // fork() ile yeni süreç oluştur
    pid_t pid = fork();
    if (pid == 0) {
        // --- CHILD PROCESS ---
        if (mode == DETACHED) setsid(); // Detached ise yeni oturum aç
        execvp(args[0], args); // Komutu çalıştır
        exit(1); // Hata durumunda çık
    } else if (pid > 0) {
        // --- PARENT PROCESS ---
        sem_wait(shm_sem);
        // Boş slot bul ve bilgileri kaydet
        int idx = -1;
        for(int k=0; k<MAX_PROCESSES; k++) { if(!shared_data->processes[k].is_active) { idx=k; break; } }
        
        if (idx != -1) {
            shared_data->processes[idx].pid = pid;
            safe_strcpy(shared_data->processes[idx].command, command, sizeof(shared_data->processes[idx].command));
            shared_data->processes[idx].is_active = 1;
            shared_data->processes[idx].mode = (ProcessMode)mode;
            shared_data->processes[idx].status = STATUS_RUNNING;
            shared_data->processes[idx].owner_pid = getpid();      
            shared_data->processes[idx].start_time = time(NULL);
            shared_data->process_count++;
        }
        sem_post(shm_sem);
        printf("[SUCCESS] Process başlatıldı: PID %d\n", pid);
        send_message(CMD_START, pid); // Diğer terminallere haber ver
    } else { perror("Fork Failed"); }
}

/* --- 6.2 PROCESS LİSTELEME --- 
list_processes fonksiyonu Shared Memory'yi okuyor. 
Tablo formatında (PID, CMD, MOD, OWNER, SÜRE) basıyor. 
sanitize ile hayaletleri temizliyor.*/
static void list_processes(void) {
    // Listelemeden önce hayalet süreçleri temizle
    sanitize_shared_memory();

    sem_wait(shm_sem); // Okurken kilit koy Race Condition önlemi
    printf("\n");
    printf("╔════════╦═══════════════════════════╦════════════╦══════════╦════════════╗\n");
    printf("║                    ÇALIŞAN PROGRAMLAR                                   ║\n");
    printf("╠════════╬═══════════════════════════╬════════════╬══════════╬════════════╣\n");
    printf("║ %-6s ║ %-25s ║ %-10s ║ %-8s ║ %-10s ║\n", "PID", "KOMUT", "MOD", "SAHIP", "SURE");
    printf("╠════════╬═══════════════════════════╬════════════╬══════════╬════════════╣\n");

    int total = 0;
    time_t now = time(NULL);
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (shared_data->processes[i].is_active) {
            double diff = difftime(now, shared_data->processes[i].start_time);
            char time_str[15]; snprintf(time_str, 15, "%.0fs", diff);
            printf("║ %-6d ║ %-25.25s ║ %-10s ║ %-8d ║ %-10s ║\n",
                   shared_data->processes[i].pid, shared_data->processes[i].command,
                   (shared_data->processes[i].mode == DETACHED) ? "Detached" : "Attached",
                   shared_data->processes[i].owner_pid, time_str);
            total++;
        }
    }
    printf("╚════════╩═══════════════════════════╩════════════╩══════════╩════════════╝\n");
    printf(" Toplam: %d process\n", total);
    sem_post(shm_sem);
}

/* --- 6.3 PROCESS SONLANDIRMA --- 
terminate_process fonksiyonu kullanıcıdan PID alıyor ve kill(pid, SIGTERM) gönderiyor. 
Kendisi silmiyor (Monitor siliyor), bu da gereksinime uygun.
*/
static void terminate_process(void) {
    int pid = get_safe_int("Sonlandırılacak PID: ", 1, 2147483647);
    if (pid == -1) return;

    sem_wait(shm_sem);
    int found = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (shared_data->processes[i].is_active && shared_data->processes[i].pid == pid) {
            found = 1;
            // Sadece sinyal gönder, temizliği Monitor Thread yapacak
            if (kill(pid, SIGTERM) == 0) {
                printf("[INFO] Process %d'e SIGTERM gönderildi\n", pid);
            } else {
                if (errno == ESRCH) printf("[UYARI] Process zaten yok.\n");
                else perror("Kill Error");
            }
            break;
        }
    }
    sem_post(shm_sem);
    if (!found) printf("  [HATA] PID %d bulunamadı.\n", pid);
}

/* --- 6.4 MONITOR THREAD --- 
monitor_thread_func her 2 saniyede bir çalışıyor. 
waitpid ve kill ile süreci kontrol ediyor. Zombie süreçleri de temizliyor.
Biten süreci Shared Memory'den siliyor ve [MONITOR] mesajı basıyor.
*/
static void* monitor_thread_func(void* arg) {
    (void)arg;
    int status;
    while (running) {
        sem_wait(shm_sem);
        for (int i = 0; i < MAX_PROCESSES; i++) {
            if (shared_data->processes[i].is_active) {
                pid_t p = shared_data->processes[i].pid;
                
                // 1. Process dışarıdan öldürülmüş mü? (ESRCH)
                int is_dead = (kill(p, 0) == -1 && errno == ESRCH);
                // 2. Bizim çocuğumuzsa ve bitmişse (waitpid)
                int my_child_dead = (shared_data->processes[i].owner_pid == getpid() && waitpid(p, &status, WNOHANG) > 0);

                if (is_dead || my_child_dead) {
                    // Temizle
                    shared_data->processes[i].is_active = 0;
                    if(shared_data->process_count > 0) shared_data->process_count--;
                    
                    // Bildir
                    printf("\n[MONITOR] Process %d sonlandı\n", p);
                    printf("Seçiniz: "); fflush(stdout);
                    send_message(CMD_TERMINATE, p);
                }
            }
        }
        sem_post(shm_sem);
        sleep(2); // 2 saniye bekle
    }
    return NULL;
}

/* --- 6.5 IPC LISTENER THREAD --- 
listener_thread_func sürekli kuyruğu dinliyor. 
Kendi mesajını (sender_pid == getpid) yutuyor (veya geri koyuyor), 
başkasının mesajını [IPC] formatında ekrana basıyor.
*/
static void* listener_thread_func(void* arg) {
    (void)arg;
    Message msg;
    while (running) {
        // Kuyruktan mesaj oku
        if (msgrcv(msgid, &msg, sizeof(Message) - sizeof(long), 0, IPC_NOWAIT) > 0) {
            
            if (msg.sender_pid == getpid()) {
                // Kendi mesajımızsa geri koy (Message Bouncing) ki diğerleri de okusun
                msgsnd(msgid, &msg, sizeof(Message) - sizeof(long), 0);
                usleep(300000); 
            } else {
                // Başkasının mesajı -> Ekrana bas
                if (msg.command == CMD_START) printf("\n[IPC] Yeni process başlatıldı: PID %d\n", msg.target_pid);
                else if (msg.command == CMD_TERMINATE) printf("\n[IPC] Process sonlandırıldı: PID %d\n", msg.target_pid);
                printf("Seçiniz: "); fflush(stdout);
            }
        } else {
            usleep(200000); 
        }
    }
    return NULL;
}

/* --- 6.6 ÇIKIŞ VE TEMİZLİK --- 
cleanup_ipc_and_exit fonksiyonu Attached süreçleri öldürüyor, Detached süreçleri koruyor. 
Son kullanıcı çıktığında Shared Memory ve Message Queue'yu unlink/msgctl ile siliyor.
*/
static void cleanup_ipc_and_exit(void) {
    int should_destroy_ipc = 0;
    sem_wait(shm_sem);

    // Attached ve sahibi biz olan süreçleri öldür
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (shared_data->processes[i].is_active && 
            shared_data->processes[i].mode == ATTACHED && 
            shared_data->processes[i].owner_pid == getpid()) {
            
            kill(shared_data->processes[i].pid, SIGTERM);
            shared_data->processes[i].is_active = 0;
            if(shared_data->process_count > 0) shared_data->process_count--;
        }
    }

    if (shared_data->instance_count > 0) shared_data->instance_count--;
    
    // Son kullanıcı ise ve process yoksa kaynakları sil
    if (shared_data->instance_count == 0 && shared_data->process_count == 0) should_destroy_ipc = 1;

    sem_post(shm_sem);
    running = 0;
    
    // Threadleri kapat
    pthread_cancel(monitor_thread);
    pthread_cancel(listener_thread);
    pthread_join(monitor_thread, NULL);
    pthread_join(listener_thread, NULL);

    if (shared_data) munmap(shared_data, sizeof(SharedData));
    if (shm_fd != -1) close(shm_fd);
    if (shm_sem) sem_close(shm_sem);

    if (should_destroy_ipc) {
        shm_unlink(SHM_NAME);//hafızayı sisteme iade eder
        sem_unlink(SEM_NAME);
        if (msgid != -1) msgctl(msgid, IPC_RMID, NULL);
        unlink(MSG_KEY_FILE);
        printf("[SİSTEM] Kaynaklar temizlendi.\n");
    } else {
        printf("[SİSTEM] Detached süreçler nedeniyle hafıza korundu.\n");
    }
    printf("ProcX kapatıldı.\n");
}

/* --- ANA PROGRAM (MAIN) --- */
int main(void) {
    int option;
    
    // Sinyal yakalama ayarı (Ctrl+C için)
    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    setvbuf(stdout, NULL, _IONBF, 0); // Anlık çıktı için buffer'ı kapat
    init_ipc(); // IPC kaynaklarını başlat

    // Arka plan işçilerini başlat
    pthread_create(&monitor_thread, NULL, monitor_thread_func, NULL);
    pthread_create(&listener_thread, NULL, listener_thread_func, NULL);

    printf("ProcX Başlatılıyor... PID: %d\n", getpid());

    while (running) {
        show_menu();
        option = get_safe_int("Seçiniz: ", 0, 3);
        if (option == -1) continue; 

        switch (option) {
            case 1: start_new_program(); break;
            case 2: list_processes(); break;
            case 3: terminate_process(); break;
            case 0: running = 0; break;
            default: printf("Geçersiz seçim.\n");
        }
    }
    cleanup_ipc_and_exit();
    return 0;
}