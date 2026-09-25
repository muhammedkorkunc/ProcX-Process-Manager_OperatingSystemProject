# ProcX - Gelişmiş Süreç Yönetim Sistemi 🚀

**Ders:** İşletim Sistemleri  
**Öğrenci:** Muhammed Emin Korkunç  
**Numara:** 2021221054

---

## 📋 Proje Tanımı

**ProcX**, Linux ve Unix tabanlı sistemlerde çalışan, çoklu işlem (multiprocessing) ve süreçler arası iletişim (IPC) temellerine dayanan gelişmiş bir süreç yönetim aracıdır. Kullanıcıların programları **Attached** (terminale bağlı) veya **Detached** (arka planda bağımsız) modda çalıştırmasına, bu süreçleri izlemesine ve yönetmesine olanak tanır.

Birden fazla terminalde çalışan ProcX örnekleri, **Paylaşılan Bellek (Shared Memory)** ve **Mesaj Kuyrukları (Message Queue)** kullanarak birbirleriyle anlık olarak senkronize olur.

---

## 🌟 Temel Özellikler

- **🛠 Process Yönetimi:** `fork()` ve `execvp()` kullanılarak yeni süreçlerin oluşturulması.
- **👻 Detached Mod (Daemon-like):** `setsid()` ile terminalden bağımsız, arka planda çalışan ve terminal kapansa bile hayatta kalan süreçler.
- **🔄 IPC & Senkronizasyon:**
  - **Shared Memory:** Tüm terminallerin ortak erişebildiği süreç tablosu.
  - **Semaphore:** Eşzamanlı erişimde veri bütünlüğünü koruyan kilit mekanizması (Race Condition önleme).
  - **Message Queue:** Terminaller arası anlık bildirim sistemi (`[IPC]` mesajları).
- **👀 Monitor Thread:** Arka planda sürekli çalışan, süresi dolan veya sonlanan süreçleri otomatik temizleyen "Çöp Toplayıcı" (Garbage Collector).
- **🛡️ Güvenlik & Dayanıklılık:** Hatalı kullanıcı girişlerine, Buffer Overflow risklerine ve Zombie süreçlere karşı tam koruma.
- **💾 Kalıcılık (Persistence):** Program kapatılıp açılsa bile Detached süreçlerin takibinin devam etmesi.

---

## 🔧 Kurulum ve Derleme

Proje, **Linux** ve **macOS** sistemlerinde çalışacak şekilde tasarlanmıştır. İçerisinde bulunan akıllı `Makefile` işletim sistemini otomatik algılar.

### 1. Derleme (Build)

Projeyi derlemek için terminalde proje dizinine gidip şu komutu yazmanız yeterlidir:

```bash
make
```

# ⚡ ProcX - POSIX Multi-Process Manager & IPC Synchronizer

A lightweight, robust multi-process management system and job controller written in **C (POSIX.1-2008)** for Linux and macOS[cite: 21, 22]. ProcX allows operators to spawn, monitor, and terminate concurrent foreground (**Attached**) and daemonized background (**Detached**) processes with real-time inter-terminal state synchronization.

---

## 🖥️ System Interface & Architecture Overview

```text
[ Terminal Instance A (ProcX) ]           [ Terminal Instance B (ProcX) ]
        │                                                │
        ├──────────────────────┬─────────────────────────┤
        ▼                      ▼                         ▼
┌──────────────────┐ ┌──────────────────┐ ┌──────────────────────────────┐
│  POSIX Semaphore │ │  Shared Memory   │ │    System V Message Queue    │
│ (/procx_sem_lock)│ │ (/procx_shm_area)│ │      (/tmp/procx_msgkey)     │
│  Mutual Exclusion│ │  PCB Table (x50) │ │ Broadcast CMD_START/TERMINATE│
└──────────────────┘ └──────────────────┘ └──────────────────────────────┘
        ▲                      ▲                         ▲
        │                      │                         │
 ┌──────┴──────────────────────┴─────────────────────────┴──────┐
 │                    Worker Process Pipeline                   │
 │ • fork() + execvp()       • setsid() Session Decoupling      │
 │ • Background Monitor Thread (waitpid / Zombie Reaper)        │
 │ • Asynchronous IPC Listener Thread (Event Notification)       │
 └──────────────────────────────────────────────────────────────┘
 🚀 Key Features & Engineering HighlightsProcess Lifecycle Management: Spawns arbitrary commands via fork() and execvp().   Session Decoupling (Daemonization): Implements detached process execution using setsid(), keeping workloads persistent across shell disconnects (similar to nohup).   Synchronized Shared State (IPC):Shared Memory (shm_open, mmap): Centralized Process Control Block (PCB) pool supporting up to 50 active tasks across multi-terminal instances.   Named POSIX Semaphore (sem_open, sem_wait): Enforces strict mutual exclusion to eliminate race conditions during concurrent state writes.   System V Message Queue (msgget, msgsnd, msgrcv): Delivers cross-instance event notifications ([IPC]) without polling overhead.   Multithreaded Garbage Collection:Monitor Thread: Periodically inspects child health with non-blocking waitpid(..., WNOHANG) to reap zombie processes and free active slots.   Listener Thread: Consumes IPC message queues asynchronously without blocking UI interactions.   Defensive Systems Programming: Sanitizes dead PIDs (kill(pid, 0)), isolates buffer overflows using size-bounded string routines, and safely cleans resources on terminal exit (SIGINT / menu exit).

 .
├── Makefile                      # Platform-aware compilation (Linux / macOS)
├── procx.c                       # Unified source implementation (POSIX C)
├── rapor.pdf                     # Academic technical design document
├── README.md                     # Technical documentation & usage
└── program_screenshot/              # Verification test logs &
    ├── test1.png                 # Build & interactive menu execution
    ├── test2.jpg                 # Spawning detached processes
    ├── test4.jpg                 # Signal termination via SIGTERM
    └── test10.png                # Attached vs. Detached lifecycle test


```
