# gach-os appstore

Kho mã nguồn app cài thêm (`.gapp`, Lua) của gach-os. Mỗi app là một thư mục trong `apps/`. Đặc tả API: [docs/sdk.md](../docs/sdk.md).

```
appstore/
├── gapp.sh          # công cụ: new | check | pack | install | list
├── template/        # khung app mới
├── apps/<tên>/      # mã nguồn app: manifest.ini + main.lua (+ file khác)
└── dist/            # đầu ra (gitignored): <id>.gapp + index.json
```

## Quy trình

```bash
./appstore/gapp.sh new weather --name "Weather"     # 1. tạo app từ template -> appstore/apps/weather
$EDITOR appstore/apps/weather/main.lua              # 2. viết app
./appstore/gapp.sh check weather                    # 3. kiểm tra manifest + cú pháp Lua
./appstore/gapp.sh install /media/$USER/SDCARD weather   # 4. đóng gói + chép vào <SD>/install/
```

Cắm thẻ vào máy, rồi khởi động lại hoặc mở app **Apps** và nhấn nút thẻ SD.

| Lệnh | Tác dụng |
| :--- | :--- |
| `new <tên> [--id org.x.y] [--name "..."]` | Tạo app mới (id mặc định `org.gachos.<tên>`) |
| `check [app...]` | Kiểm tra manifest, file entry, cú pháp Lua (nếu có `luac`) |
| `pack [app...]` | Đóng gói vào `dist/` và sinh `dist/index.json`. Không truyền tên = toàn kho, dọn gói cũ trước |
| `install <SD> [app...]` | `pack` rồi chép gói vào `<SD>/install/` |
| `list` | Liệt kê app trong kho |

## Quy ước

- **id** là duy nhất và không bao giờ đổi sau khi phát hành (máy dùng id để nâng cấp và giữ dữ liệu).
- Tăng `version` mỗi lần phát hành. Chỉ tăng `sdk` khi app dùng API của SDK mới hơn.
- Mỗi app khai báo **tối thiểu** quyền cần thiết (`permissions=`).
- Commit theo chuẩn dự án với scope là tên app, VD: `feat(calculator): ...`.
- Ảnh để dạng **PNG** trong thư mục app (SDK v8); `pack` tự đổi sang `.gimg` cho board. Công cụ phát triển (VD script
  sinh đồ họa) đặt trong thư mục `tools/` của app: không được đóng vào gói.

## index.json: danh mục cho kho ứng dụng

`pack` sinh `dist/index.json` mô tả mọi gói (id, tên, phiên bản, sdk, icon, quyền, kích thước, SHA-256). Khi có kho tải qua Wi-Fi, server chỉ cần phục vụ thư mục `dist/`; máy đọc `index.json`, tải `.gapp`, kiểm tra SHA-256 rồi ghi vào `/install`.

## App hiện có

| App | SDK | Minh họa |
| :--- | :---: | :--- |
| [calculator](apps/calculator) | 2 | Máy tính bỏ túi kiểu điện thoại: lưới co giãn, nút tròn, ký hiệu vẽ tay |
| [coreguard](apps/coreguard) | 8 | Game thủ thành theo làn: `gach.game` (sprite sheet, va chạm, chạm-chọn), ảnh PNG |
| [counter](apps/counter) | 1 | UI, sự kiện, timer, `gach.storage` |
| [spritebench](apps/spritebench) | 8 | Đo engine `gach.game`: 100 -> 1000 sprite, số đo ghi vào log |
| [uikit](apps/uikit) | 8 | Mọi widget / sự kiện, ảnh và canvas |
| [world_clock](apps/world_clock) | 1 | Danh sách, `gach.sys.time()`, `gach.app.exit()` |
