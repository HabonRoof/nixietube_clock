# A5 直式右裝訂說明書

本冊共 32 頁，內文橫排、書脊位於右側。第 2 頁為跨頁右半，第 3 頁為左半；封面、封底使用純文字。

## 交付檔

- `../../../output/pptx/nixie_clock_booklet_zh_TW_final.pptx`：PowerPoint 編輯版，每張投影片是一頁 A5（148 × 210 mm）。
- `../../../output/pdf/nixie_clock_booklet_zh_TW.pdf`：依閱讀順序排列的單頁 PDF，300 dpi 渲染平面化，供閱讀與列印；可編輯文字保留在 PPT。
- `../../../output/pdf/nixie_clock_booklet_A4_RTL_print.pdf`：A4 橫式右裝訂拼版，共 16 面，雙面列印使用 8 張紙。

所有內文、標題、頁碼與編號標註均為可編輯文字，字型統一為 Arial Unicode MS，與前版 PDF 相同。3D 圖及手機介面為 PNG 圖片；需要修改 WebUI 文字或 3D 場景時，請用下列來源重新產生圖片後替換。未嵌入商用字型檔，在其他電腦編輯需安裝同名字型以維持排版。

## 列印

使用已拼版的 A4 PDF，選 A4 橫式、雙面短邊翻轉、100% 實際大小；不要再次啟用印表機的「小冊子」功能。先試印第一張確認正反方向，再印全部、依序套疊並沿中央對折。封面位於外側左半，折後書脊在右側。

A5 文件未加印刷出血。滿版深色需支援無邊界的設備，或交由印刷店使用較大紙張裁切；一般桌上型印表機可能保留白邊。商業印刷可將單頁 PDF 交由印刷店拼版，告知右裝訂。

## 圖片來源與尺寸

最終跨頁採 `assets/cafe_photo_composite.png`，以原始模型的 `front_controls.png` 作產品參考，生成寫實照片。依時鐘及杯子規格約束視覺比例，保留可見的三顆頂部按鈕、清除前景雜物；生成式圖片不作精密尺寸依據。以下完整 Blender 咖啡廳場景保留為尺寸研究草稿，不用於最終跨頁。

`render_front_controls.py` 使用一般 50 mm 相機正面略俯視。按鈕位置標線直接取模型中心投影，使用名稱「開始／停止按鈕」「模式切換按鈕」「顏色循環按鈕」。工程代號對照僅保留於 PPT 備忘稿。

- `render_cafe_scaled.py`：直接載入 Revision H 原始模型，整機 260 × 80.12 × 84 mm，不縮放時鐘。杯子以 KINTO SCS 250 ml 公開規格 φ80 × H90 × W105 mm 製作通用近似模型；書本為 A5 148 × 210 mm、B6 128 × 182 mm，厚度分別為示意的 18、22 mm。層架為 1100 × 320 × 24 mm。
- 杯子規格：https://kinto-europe.com/products/27635 。杯子模型非原廠 CAD；場景為 3D 示意，非照片。
- `cafe_scene_scaled.blend`：完整情境場景，可直接在 Blender 編輯。相機為一般 PERSP，48 mm。
- `render_modes.py`：五種模式統一使用一般 PERSP 透視相機、50 mm 焦距、36 mm 感光元件寬，無鏡頭位移或魚眼。數字直接使用原模型的陰極網格。
- `render_hardware.py`：爆炸圖沿原模型面板法線平移，保留各零件旋轉與裝配關係。工程爆炸圖使用正交視圖；模式圖與情境圖使用一般透視相機。爆炸圖的間距為辨識示意，非拆卸順序或尺寸圖。
- 硬體原檔：`hardware/case/revision_h/clock_case_revision_h.blend`。不改寫原檔。
- `capture_mobile.cjs`：從 `src/web_page.cpp` 擷取 WebUI，以示範 API 回應渲染。Android 使用 Chrome 行動尺寸；iPhone 使用 Playwright WebKit 的 iPhone 13 預設裝置設定，非 Xcode iOS Simulator 或實機驗收。
- 四組燈光設定檔以橘、藍、綠、紫示範，儲存與套用流程有完整步驟。

## 重建

1. Blender 背景模式執行 `render_cafe_scaled.py` 及 `render_hardware.py`。
2. Blender 載入 Revision H `.blend` 後執行 `render_modes.py`。
3. Node 執行 `capture_mobile.cjs android` 與 `capture_mobile.cjs ios`（需要 Playwright、Chrome 與 WebKit runtime）。
4. Node 執行 `build_booklet.mjs`（需要 `@oai/artifact-tool` 及 `sharp`），產生 `tmp/booklet/candidate.pptx` 和逐頁預覽。
5. 執行工作目錄 `tmp/booklet/finalize.mjs` 完成封裝與幾何檢查，使用新檔名輸出正式版。以簡報渲染器逐頁檢查，並以 300 dpi 圖片建立閱讀 PDF。隨附 LibreOffice 在此環境缺少可用中文字型，其試匯出檔不供交付。
6. `python impose_booklet.py 單頁.pdf 拼版.pdf` 產生右裝訂拼版。

32 頁內容以 `build_booklet.mjs` 為來源。Omnixie 手冊僅作章節與逐步圖說編排參考，未套用其硬體功能。各頁備忘稿保留主要來源及模擬範圍。


工具已集中至 `scripts/user_guide/`；目前機殼採 Revision L。上述 Revision H 渲染與報告為歷史資料，重跑限制見專案根目錄的 `scripts/README.md`。
