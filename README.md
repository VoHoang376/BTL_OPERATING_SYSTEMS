<<<<<<< Updated upstream
# BTL_OPERATING_SYSTEMS
=======
# Simple Operating System (OSSIM) - BTL Hệ Điều Hành (CO2018)

Dự án mô phỏng hệ điều hành đơn giản (**Simple Operating System**) thuộc bộ môn Khoa Học & Kỹ Thuật Máy Tính - ĐHBK TP.HCM.

---

## 1. Cấu Trúc Toàn Bộ Dự Án (Project Structure)

```
BTL_OPERATING_SYSTEMS/
├── Makefile                          # Lệnh biên dịch toàn bộ hệ điều hành
├── README.md                         # Tài liệu hướng dẫn kiến trúc và chạy thử
├── README_COPROCESSOR.md             # Báo cáo kỹ thuật chi tiết phần Co-processor (TV3 & TV4)
├── include/                          # Thư mục khai báo giao diện và cấu trúc dữ liệu
│   ├── os-cfg.h                      # Cấu hình hệ thống (MLQ_SCHED, MAX_PRIO 140, MM64, MM_PAGING...)
│   ├── common.h                      # Cấu trúc PCB (pcb_t), Kernel (krnl_t), opcodes, instructions
│   ├── bitopts.h                     # Macro thao tác bit
│   ├── queue.h                       # Cấu trúc hàng đợi queue_t
│   ├── sched.h                       # Giao diện bộ lập lịch MLQ & đăng ký PID
│   ├── cpu.h                         # Mô phỏng CPU đa nhân, vòng lặp thực thi lệnh & kiểm tra ngắt
│   ├── timer.h                       # Bộ đếm xung nhịp thời gian hệ thống
│   ├── loader.h                      # Đọc và nạp chương trình vào PCB
│   ├── os-mm.h                       # Cấu trúc bộ nhớ ảo (vm_area, vm_rg) và vật lý (memphy)
│   ├── mm.h                          # Giao diện MMU tổng quát
│   ├── mm64.h                        # Phân trang 5 cấp 64-bit (PGD -> P4D -> PUD -> PMD -> PT)
│   ├── mem.h                         # Module bộ nhớ cũ (obsolete / giữ để tương thích)
│   ├── syscall.h                     # Giao diện System Call (listsyscall, memmap, sys_xxxhandler)
│   ├── dma.h                         # Bộ đồng xử lý DMA (Direct Memory Access Co-processor)
│   ├── libmem.h                      # Thư viện quản lý bộ nhớ phía người dùng (alloc, free, read, write)
│   └── libstd.h                      # Thư viện chuẩn & cơ chế copy User/Kernel qua PID
├── src/                              # Toàn bộ mã nguồn C hoàn thiện ở mức cơ bản nhất
│   ├── os.c                          # Khởi động OS, cấp phát RAM/SWAP, khởi tạo Scheduler/CPU/Timer
│   ├── sched.c                       # Thuật toán MLQ chuẩn: slot = MAX_PRIO - prio, khóa Mutex
│   ├── queue.c                       # Thao tác hàng đợi ưu tiên (enqueue, dequeue, empty)
│   ├── cpu.c                         # Vòng lặp CPU: fetch & decode chỉ lệnh, xử lý ngắt DMA
│   ├── timer.c                       # Luồng timer đếm thời gian
│   ├── loader.c                      # Nạp file chương trình từ input/proc/
│   ├── mm64.c                        # Phân trang 5 cấp 64-bit: PGD -> P4D -> PUD -> PMD -> PT
│   ├── mm-vm.c                       # Quản lý không gian ảo, sbrk và free regions list
│   ├── mm-memphy.c                   # Mô phỏng thiết bị phần cứng RAM & SWAP, frame lists
│   ├── mm.c                          # Giao diện cấp phát frame vật lý & mapping
│   ├── dma.c                         # Bộ điều khiển DMA: giao tiếp bất đồng bộ, Bus addressing, IRQ
│   ├── syscall.c                     # Dispatcher điều phối System Calls
│   ├── sys_xxxhandler.c              # Handler mở rộng cho syscall 440
│   ├── libmem.c                      # API bộ nhớ: alloc, free, read, write (dịch địa chỉ ảo sang vật lý)
│   ├── libstd.c                      # Truyền dữ liệu User/Kernel an toàn qua PID (bảo vệ bộ nhớ)
│   ├── mem.c                         # Module memory cũ (obsolete)
│   └── paging.c                      # Module paging cũ (obsolete)
├── input/                            # File cấu hình và tiến trình thử nghiệm
│   ├── os_default                    # Kịch bản kiểm thử mặc định (chạy p0, p1, sc)
│   ├── os_dma                        # Kịch bản kiểm thử riêng cho Co-processor DMA
│   ├── os_syscall                    # Kịch bản kiểm thử riêng cho System Call
│   └── proc/                         # Mã lệnh các tiến trình mẫu
│       ├── p0                        # Kiểm thử bộ nhớ: calc, alloc, write, read
│       ├── p1                        # Kiểm thử DMA: dma_memcpy
│       ├── p_dma                     # Kiểm thử DMA bất đồng bộ chạy song song phép tính CPU
│       └── sc                        # Kiểm thử System Call: syscall 440
└── output/                           # Thư mục lưu kết quả xuất mô phỏng
```

---

## 2. Tóm Tắt Nhiệm Vụ 4 Thành Viên Đã Hoàn Thành

### Thành viên 1: Lập Lịch MLQ & Đồng Bộ Hóa (2.0 điểm)
- **Cài đặt Multi-Level Queue**:
  - Hệ thống gồm `MAX_PRIO = 140` hàng đợi ưu tiên.
  - Mỗi hàng đợi được cấp số slot CPU: `slot = (MAX_PRIO - prio)`.
  - Hàm `get_proc()` ưu tiên lấy tiến trình từ hàng đợi cao nhất còn slot; khi tất cả hàng đợi đều hết slot, tự động reset slot cho vòng lập lịch mới.
- **Đồng bộ hóa (Lock/Mutex)**:
  - Khóa `pthread_mutex_t queue_lock` bảo vệ mọi thao tác trên hàng đợi khi nhiều CPU chạy song song.

### Thành viên 2: Multi-level Paging 64-bit, MMU & Phân Tách User/Kernel (2.0 điểm)
- **Phân trang 5 cấp 64-bit (`mm64.c`)**:
  - Dịch địa chỉ theo kiến trúc: PGD (L5) $\rightarrow$ P4D (L4) $\rightarrow$ PUD (L3) $\rightarrow$ PMD (L2) $\rightarrow$ PT (L1) và Offset 12 bit (4KB page).
  - Kiểm tra chuẩn Canonical Address (bit 63..57).
  - Cấp phát bảng trang động (demand paging).
- **Thao tác bộ nhớ (`libmem.c`)**:
  - `ALLOC`: nâng `sbrk`, lấy frame từ RAM và map vào 5-level page table, lưu địa chỉ ảo vào thanh ghi.
  - `WRITE`: dịch địa chỉ ảo sang vật lý và ghi byte vào RAM.
  - `READ`: dịch địa chỉ ảo sang vật lý và đọc byte từ RAM lưu vào thanh ghi.
  - `FREE`: giải phóng vùng nhớ ảo.
- **Phân tách User / Kernel Space (`libstd.c`)**:
  - Giao tiếp giữa Kernel và User Space hoàn toàn thông qua `PID` (hàm `get_pcb_by_pid()`), **không truyền trực tiếp con trỏ PCB**.
  - Ngăn chặn hoàn toàn việc User program đọc/ghi trực tiếp vào vùng nhớ Kernel Space.

### Thành viên 3: Core DMA & Bus Addressing (2.0 điểm)
- **Giao tiếp bất đồng bộ**:
  - CPU offload tác vụ cho DMA qua `dma_trigger_async()` và trả về ngay (`Non-blocking`).
- **Bus Addressing**:
  - Hàm `get_ptr()` ánh xạ địa chỉ bus vào trực tiếp RAM vật lý `kernel.mram->storage`.
  - Hàm `dma_copy()` sao chép trực tiếp dữ liệu trên RAM byte-by-byte (tránh lệch Endianness) và in log debug chuẩn theo Appendix Trang 33.

### Thành viên 4: Hardware Interrupts & CPU Integration (1.0 điểm)
- **Xử lý ngắt phần cứng**:
  - Luồng phần cứng `dma_worker` phát ngắt `irq = 1` khi truyền xong dữ liệu.
  - CPU kiểm tra đường ngắt và thực thi trình phục vụ ngắt `dma_isr()` để acknowledge và hạ tín hiệu ngắt. Hỗ trợ đa nhân SMP an toàn.
- **Tích hợp CPU**:
  - Giải mã lệnh `DMA_MEMCPY` trong chu kỳ CPU, giải phóng CPU tiếp tục chạy các lệnh tính toán mà không bị nghẽn (khắc phục hoàn toàn Programmed I/O).

---

## 3. Hướng Dẫn Biên Dịch & Chạy Thử

### Biên dịch:
```bash
# Trong thư mục BTL_OPERATING_SYSTEMS:
gcc -Wall -Wextra -pthread -Iinclude -o os src/*.c
```

### Chạy kiểm thử hệ thống tổng hợp:
```bash
./os input/os_default
```

### Chạy kiểm thử chuyên biệt DMA Co-processor:
```bash
./os input/os_dma
```

### Chạy kiểm thử System Call:
```bash
./os input/os_syscall
```
>>>>>>> Stashed changes
