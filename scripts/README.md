# 開發工具與遠端操作

專案維護的 Python 腳本集中於本目錄，所有命令從專案根目錄執行。

- `generate_git_version.py`：由 PlatformIO 的兩個環境呼叫，產生目前 Git commit 的編譯巨集；需安裝 Git。
- `test_cli.py`：透過序列埠發送指令的互動式硬體測試，需安裝 `pyserial`。測試步驟見 [CLI 測試說明](../test/test_cli/README.md)。
- `mock_web_ui.py`：沒有開發板時，在瀏覽器預覽設定頁。只使用 Python 標準庫。

## 沒有硬體時預覽設定頁

設定頁的 HTML 嵌在 `src/web_page.cpp`，瀏覽器不能直接打開那個 C 檔。`mock_web_ui.py` 會抽出同一份頁面，並用記憶體裡的假資料回應頁面會呼叫的 API（`/api/settings`、`/api/time`、`/api/wifi`、`/api/ap/status`、`/api/ap/stop`、`/api/audio/tracks`、`/api/audio/status`、`/api/audio/play`）。

```sh
python3 scripts/mock_web_ui.py
```

然後打開 `http://127.0.0.1:8080/`。埠被占用時改用 `python3 scripts/mock_web_ui.py --port 8765`。

假資料在程式結束後就消失。終端機會印出每一筆會改設定的請求：拖曳燈光是 `persist: false`，只記在預覽、重新整理後會回到已儲存的設定檔；按「儲存」或調整音量、鬧鐘、休眠才寫進假的設定。頁面一開始是已校時的時鐘，所以不會一打開就自動送出對時。

## Cursor 遠端虛擬機

連上虛擬機後，用 Cursor 開啟完整 Git checkout 的根目錄。在虛擬機的終端使用獨立 Python 環境：

```sh
python3 -m venv /tmp/nixie-clock-venv
. /tmp/nixie-clock-venv/bin/activate
python -m pip install platformio pyserial
python scripts/generate_git_version.py
pio run -e esp32_s3_nixie
```

`/tmp` 環境可能在重啟後清除，屆時重新建立即可。編譯需能下載 PlatformIO 與 ESP-IDF 依賴；目前韌體依賴的框架版本請一併參照根目錄 README。本次整理未在遠端 VM 驗證韌體編譯。

若虛擬機已取得實體裝置的 USB／序列埠（以下埠名僅為範例）：

```sh
pio run -e esp32_s3_nixie -t upload --upload-port /dev/ttyUSB0
python scripts/test_cli.py /dev/ttyUSB0
```

測試會改變數字與背光，需觀察實體時鐘並按 Enter 繼續；執行前先關閉占用相同埠的 serial monitor。只有 SSH 連線不會自動將本機 USB 傳給 VM，沒有裝置時可先進行編譯。

## 模型與文件

目前保留的模型和列印檔見 [Revision L](../hardware/case/revision_l/README.md)。本分支已沒有早期裝配、IN-4 建模、焊接治具或 user guide 渲染腳本，也没有它們的完整依賴；不要依照舊說明嘗試執行。現有模型不需要這些腳本即可開啟。
