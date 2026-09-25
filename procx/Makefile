# ==========================================
# Makefile - ProcX 
# Yazar: Muhammed Emin Korkunç
# ==========================================

CC = gcc
CFLAGS = -Wall -pthread -D_XOPEN_SOURCE=700

TARGET = procx
SRC = procx.c

# İşletim sistemini algıla
UNAME_S := $(shell uname -s)
# Mevcut kullanıcı adını al (Temizlik için)
CURRENT_USER := $(shell whoami)

# Varsayılan kütüphaneler (boş)
LIBS = 

# Eğer Linux ise -lrt kütüphanesini ekle
ifeq ($(UNAME_S),Linux)
	LIBS += -lrt
endif

# Varsayılan hedef
all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LIBS)

# Temizlik (Sadece dosyayı siler)
clean:
	rm -f $(TARGET)

# --- DERİN TEMİZLİK (DEEP CLEAN) ---
# Kilitli kalan TÜM kaynakları siler.
ipc-clean:
	@echo "[TEMİZLİK] IPC Kaynakları taranıyor..."
	
	@# 1. Mesaj Kuyruklarını Sil (Message Queues)
	@ipcs -q | grep $(CURRENT_USER) | awk '{print $$2}' | xargs -n 1 ipcrm -q 2>/dev/null || true
	
	@# 2. Paylaşılan Belleği Sil (Shared Memory)
	@ipcs -m | grep $(CURRENT_USER) | awk '{print $$2}' | xargs -n 1 ipcrm -m 2>/dev/null || true
	
	@# 3. Semaforları Sil (Semaphores)
	@ipcs -s | grep $(CURRENT_USER) | awk '{print $$2}' | xargs -n 1 ipcrm -s 2>/dev/null || true
	
	@# 4. Linux için dosya tabanlı temizlik
	@rm -f /dev/shm/*procx* 2>/dev/null || true
	@rm -f /tmp/procx_msgkey 2>/dev/null || true
	
	@echo "✨ [BAŞARILI] Sistem tamamen sıfırlandı!"