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
