# 開發與製作工具

目前暫定機殼版本為 `hardware/case/revision_l/`，列印與裝配檔以該目錄為準。
請從專案根目錄執行以下工具；Blender、FreeCAD、KiCad 與文件工具的執行環境依各子目錄說明。

- `generate_git_version.py`：PlatformIO 使用的 Git 版本巨集，已更新 `platformio.ini`。
- `hide_description_field.py`：KiCad 原理圖欄位整理工具，需提供輸入檔案。
- `assembly_preview/`：早期外殼與六管裝配預覽工具及參考產物。
- `assembly_validation/`：PCB、管腳、模型裝配檢查工具、模型與檢查資料。
- `in4_tube_model/`：IN-4 建模、簡化、驗證工具及依賴模型。
- `socket_solder_jig/`：焊接治具概念工具與尺寸資料。
- `user_guide/tools/`：使用指南截圖、渲染及 PDF 產生腳本。
- `user_guide/booklet/`：小冊子渲染、模型匯出與文件產生腳本。

文件內容與圖片仍位於 `doc/user_guide/`，交付產物保留在 `output/`。
`tmp/` 為可刪除的工作目錄，已加入 Git 忽略規則；重跑個別工具前需建立所需暫存子目錄。

## 舊版工具限制

早期裝配工具仍依賴已不在工作目錄內的 `OUTBOXX_B/F.STL`，部分說明書渲染工具仍依賴 Revision H 及其物件名稱。這些工具與既有報告保留作歷史參考，不能當成 Revision L 的驗證結果；本次整理沒有將舊版幾何假設套用到 L 版。若要重新渲染或驗證 L 版，需先適配模型物件與尺寸。
