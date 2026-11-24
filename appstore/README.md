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

## Trang app trong Gach Store

| Trường / file | Ý nghĩa |
| :--- | :--- |
| `category=` trong `manifest.ini` | Thể loại hiển thị (Games, Utilities, Developer, Weather...) |
| `description=` | Mô tả 1–2 câu (một dòng, không TAB) |
| `store/1.png` .. `store/3.png` | Ảnh chụp trang chi tiết (không đóng vào gói; `pack` đổi sang `dist/shots/<id>-<n>.gimg`). Chụp ngay trên board: mở app rồi `tools/gach screenshot 3 appstore/apps/<tên>/store/1.png` (426x266) |

## Kho trực tuyến (GitHub Pages)

`pack` sinh trong `dist/`: các gói `.gapp`, `index.tsv` (danh mục board đọc: id, tên, phiên bản, sdk, kích thước,
SHA-256, file, icon, màu), `index.json` (cùng nội dung, cho công cụ khác) và `index.html` (xem bằng trình duyệt).
Máy chủ chỉ cần phục vụ tĩnh thư mục này.

| Việc | Cách làm |
| :--- | :--- |
| Bật kho (một lần) | Repo công khai → Settings → Pages → Build and deployment → Source: **GitHub Actions** |
| Phát hành / cập nhật app | Tăng `version` trong `manifest.ini`, push lên `main`: workflow [`appstore.yml`](../.github/workflows/appstore.yml) đóng gói và đăng lên `https://tranthanh1699.github.io/gach-os/` |
| Cài trên máy | App **Gach Store** → Discover / Updates → GET / UPDATE (chạm thẻ để xem chi tiết, ảnh chụp). Hoặc `tools/gach store install <id>` |
| Thử kho trên máy tính (LAN) | `python3 -m http.server 8000 -d appstore/dist` rồi `tools/gach store url http://<ip-máy-tính>:8000` (về mặc định: `gach store url default`) |

Máy tải `index.tsv`, tải gói thẳng vào thẻ SD (`install/<file>.part`), kiểm kích thước + SHA-256, rồi mới cài
(gói hỏng bị xóa, không cài). Dữ liệu `gach.storage` của app được giữ khi cập nhật. Địa chỉ kho lưu trong NVS.

## App hiện có

| App | SDK | Minh họa |
| :--- | :---: | :--- |
| [calculator](apps/calculator) | 2 | Máy tính bỏ túi kiểu điện thoại: lưới co giãn, nút tròn, ký hiệu vẽ tay |
| [coreguard](apps/coreguard) | 8 | Game thủ thành theo làn: `gach.game` (sprite sheet, va chạm, chạm-chọn), ảnh PNG |
| [counter](apps/counter) | 1 | UI, sự kiện, timer, `gach.storage` |
| [spritebench](apps/spritebench) | 8 | Đo engine `gach.game`: 100 -> 1000 sprite, số đo ghi vào log |
| [uikit](apps/uikit) | 8 | Mọi widget / sự kiện, ảnh và canvas |
| [world_clock](apps/world_clock) | 1 | Danh sách, `gach.sys.time()`, `gach.app.exit()` |
