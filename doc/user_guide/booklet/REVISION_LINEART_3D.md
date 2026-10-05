# 深色線稿與原生 3D 修訂版

正式檔：`output/pptx/nixie_clock_booklet_zh_TW_lineart_3d.pptx`。保留原版，32 頁 A5 右裝訂。

- 四張線稿由原 `output/images/` 線稿透過內建 ImageGen 改成深褐色背景與白線，存於 `assets/line_*.png`。提示要求僅改色、保留形狀與視角、不新增物件、文字或光暈。
- 模式頁使用正面線稿與六個原生文字物件，數字可個別編輯。
- 原有手機截圖未重拍，連線說明統一 Chrome，頁籤使用原截圖底部裁切。第 5 項按現有頁面解讀為「音量與曲目」共用一張音訊截圖。
- 13 個可重用 GLB 資產，18 個原生 PowerPoint 3D 物件，分布於第 25、27、29、30 頁。物件用 `3D front_shell` 等名稱，可在選取窗格選取。
- 第 31 頁 QR Code 目標為 https://github.com/HabonRoof/nixietube_clock 。

## 3D 操作

在 PowerPoint 選取零件，使用「3D Model」頁籤的視角預設或格式窗格調整角度。零件的大小與位置可分別修改。不支援原生 3D 的檢視器使用 PNG 備用影像。

## 建置來源

- `build_booklet_v2.mjs`：編排與可編輯內容，輸出至 `tmp/booklet-v2/`。
- `export_models.py`：讀取 Revision H 場景，逐組輸出中心化 GLB，不改寫原始模型。
- `embed_models.py`：把對應圖片位置換成 `am3d:model3d` 與圖片 fallback，跨頁重用模型二進位資料。
- OOXML 格式參考： https://github.com/shbernal/ts-pptx/blob/master/test/read/fixtures/README.md 的 PowerPoint 原生 model3d fixture，以及 Microsoft Model3D 文件。
- 封裝、字型及頁面幾何檢查紀錄：`tmp/booklet-v2/delivery.validation.json`。

驗證時已用 PowerPoint 開啟，選取第 25 頁前殼，實際切換 Top 視角再復原。正式檔未儲存測試變更。

## 木紋材質修正

修正版：`output/pptx/nixie_clock_booklet_zh_TW_textured_3d.pptx`。前版 GLB 未包含程序木紋，旋轉後會使用白色預設基底。現已透過 `bake_wood_models.py` 將原始前殼、背殼與托架的木紋烘焙成 2048×2048 sRGB 基底色貼圖，UV 與 PNG 都嵌入 GLB。`replace_wood_models.py` 只替換三個模型媒體部分，其他投影片內容不变。PowerPoint 實測切換前殼 Back 視角仍保留木紋，測試後復原且未儲存測試操作。
