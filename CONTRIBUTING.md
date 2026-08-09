# Contributing to remote-sensing-pipeline

Cảm ơn bạn quan tâm đến project! File này hướng dẫn cách setup môi trường dev và quy tắc đóng góp code.

## Cấu trúc project

- `cpp-core/` — backend xử lý ảnh viễn thám (C++, GDAL, ONNX Runtime, tiling, inference)
- `frontend/` — giao diện web (React + Vite)
- `database/` — schema và migration cho PostGIS
- `docs/` — tài liệu, sơ đồ kiến trúc
- `tools/` — script hỗ trợ (test data, benchmark...)

## Yêu cầu môi trường

- Docker + Docker Compose
- Node.js 20+ và npm (cho frontend)
- Git

## Setup lần đầu

1. Clone repo:
   ```bash
   git clone https://github.com/Dinhthuy2k5/remote-sensing-pipeline.git
   cd remote-sensing-pipeline
   ```

2. Cài git hooks (husky + commitlint):
   ```bash
   npm install
   ```
   (Script `prepare` sẽ tự kích hoạt husky)

3. Copy file `.env.example` thành `.env` ở gốc repo, điền giá trị thật (đặc biệt là `POSTGRES_PASSWORD`):
```bash
   cp .env.example .env
```

4. Cài dependency frontend:
   ```bash
   cd frontend
   npm install
   cd ..
   ```

5. Build và chạy toàn bộ stack:
   ```bash
   docker compose up -d --build
   ```

## Quy trình đóng góp

1. Tạo nhánh mới từ `main`:
   ```bash
   git checkout -b feat/ten-tinh-nang
   ```
2. Code, commit theo chuẩn [Conventional Commits](https://www.conventionalcommits.org/) (commitlint sẽ tự kiểm tra):
   - `feat: thêm tính năng X`
   - `fix: sửa lỗi Y`
   - `docs: cập nhật tài liệu Z`
   - `chore: việc vặt không ảnh hưởng logic`
   - `test: thêm/sửa test`
3. Trước khi commit, husky tự chạy lint cho phần frontend nếu có file `.ts/.tsx/.js/.jsx` thay đổi. Sửa hết lỗi lint trước khi commit qua được.
4. Push nhánh, mở Pull Request vào `main`. Đảm bảo CI (build frontend, build C++ core) và CodeQL pass trước khi merge.

## Coding style

- **Frontend**: tuân theo cấu hình ESLint có sẵn trong `frontend/`. Chạy `npm run lint` trong `frontend/` để tự kiểm tra.
- **C++**: theo style hiện có trong `cpp-core/` (đặt tên biến, indent...). Format bằng `clang-format` nếu có cấu hình `.clang-format` trong repo.

## Báo lỗi / đề xuất tính năng

Mở Issue mới trên GitHub, mô tả rõ:
- Bước để tái hiện lỗi (nếu là bug)
- Kết quả mong đợi vs kết quả thực tế
- Môi trường (OS, phiên bản Docker...)