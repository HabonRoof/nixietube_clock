# IN-4 雙板機構驗證與組裝

## 結論

這是依現有 PCB / STL 的機構檢查與估算高度試組裝，**尚不能宣告實物裝配全部通過**。PCB 原始焊盤、角度、走線、net、固定孔均未修改；修改僅限 3D model 節點。

### 兩板連接器

以四個 Ø3.2 mm 固定孔配準，主板座標加 (34,30) mm 即對應顯示板。兩板孔距均為 144 × 40 mm。

- 主板 J3 ↔ 顯示板 J1：2×5、2.00 mm pitch；插針／插孔中心整組沿 X 偏 **0.250 mm**。
- 主板 J4 ↔ 顯示板 J3：同樣沿 X 偏 **0.250 mm**，無相對旋轉。
- 主板 J3、J4 均向 X 減少 0.25 mm 可在固定孔完全對齊時消除偏差。這是修改建議，**本次沒有移動 footprint 或走線**。
- 比較的是插孔中心，而不是 SMD 焊盤中心：顯示板焊盤兩排相距 6.25 mm，實際 mating 兩排為 2 mm，不應混用。
- 按 pin number 比對，SDA/SCL、電源、OE、LED data 與六個 anode 的配對關係相符。J3 pin 8 在主板 NC、顯示板 GND；J4 pin 5/7 為主板額外 T7/T6、顯示板 NC；是未使用訊號，非左右排倒接。PGND1/GND 與不同層級 net 名稱按用途對應，未執行全板電性審核。

### IN-4 footprint 與管腳

- 六個 footprint 的設定角度皆為 7.887°，孔形狀彼此一致。
- 依 RSH31 14 腳、Ø18 mm、24.5° pitch / 41.5° indexing gap，並以電極名稱配對，旋轉配準後每組 RMS 誤差 **0.0428 mm**，最大 **0.0717 mm**。
- 舊 IN4_nominal 模型直接採用了 bottom-view 腳位座標；不能不處理鏡像就拿正面模型宣告 footprint 錯誤。新附加的 tube STEP 與組裝中的 pins/feedthroughs 使用 Y 鏡射以建立暫定字形基準，原始 IN4_nominal.blend 未改。
- 在此暫定字形基準下，最佳配準角為 **−7.841°（PCB X 向右、Y 向下）**，因此預覽有約 7.84° 傾斜。**這不是實物字形傾斜的已確認量測**；底視圖本身不足以證明 pin 7 與實際數字上方的關係，需用實物辨識該關係後再決定是否調整 footprint。
- 附件 [pin_alignment.svg](pin_alignment.svg) 顯示原始孔位、配準後管腳與未旋轉基準。小誤差代表孔圈形狀接近；不等於緊配 socket 孔一定插得進去。

### 六管中心與外殼

顯示板映射到外殼：X 減 55、Y 減 121 mm。六個外殼孔中心為 (35+36i,30) mm。

- N1–N3 配準中心相對理想中心約 (−0.0226,+0.0959) mm。
- N4 約 (+0.0104,+0.0959) mm；N5–N6 約 (+0.0104,+0.0096) mm。
- 最大中心偏移約 **0.099 mm**；沒有一支特別大幅偏離，N4–N6 的微小座標差異保留原狀。
- STL 面孔最窄段約 **Ø31 mm**，入口倒角約 Ø33 mm。以現有 Ø30 mm 管模型，理論最小徑向餘量約 **0.39 mm**；STL 多邊形與偏心已考慮至約 0.01 mm 等級。
- IN-4 粗網版本資料列最大玻璃直徑 Ø32 mm，不能以 Ø30 mm 名義模型保證所有實物都能穿過 Ø31 mm 孔。需量這六支實際最寬玻璃位置。

### 金屬 socket 與高度試組裝

socket 根據 IMG_4339.JPG 的 10 mm 網格估算，使用附近約 19.5 pixel/mm（聊天縮圖座標尺度），外形辨識含透視及邊缘誤差。

- 全長約 14.3 mm；上套筒 Ø1.5 × 5 mm；凸肩 Ø1.7 × 1.5 mm。
- 凸肩下方軸段 Ø1.2 × 3.2 mm；焊尾 Ø0.6 × 4.6 mm。
- 直徑約 ±0.2 mm、長度約 ±0.5 mm 僅作工作估算範圍，並非量測校正或統計信賴區間。
- 內孔 Ø1.05 mm 與有效深度為**建模假設**，照片不能量出。焊孔 Ø1.59766 mm，若凸肩真是 Ø1.7 mm，單邊承靠只有約 0.05 mm；請量凸肩與穿板軸徑。
- 以凸肩底貼 PCB 正面、管腳插入 5 mm 建模，管底高於顯示板正面 8.5 mm。玻璃突出前面板約 7 mm，沿用側照估計。
- 試組裝顯示板正面 case Z=32.5；主板正面 Z=40.735；兩板厚度均 1.6 mm；面間空氣間距約 6.635 mm，來自現有通用 header/socket 模型的 housing 接觸位置，非實際料號量測。
- socket 尾端到主板正面的名義距離僅 **0.435 mm**；未納入焊錫、板翹、修腳與製造公差。需確認是否剪腳及實際疊高。
- 本次 PCB/component 與原始外殼的三角面交叉檢查未發現交叉；此檢查不涵蓋完全包覆的實體、所有內部零件互撞、線材、電池、焊錫或高壓爬電距離。
- U9 採用**暫定 12 mm 模組高度**時距後蓋只約 0.08 mm；它是高度敏感度案例，**不能據此認定真實 U9 會碰蓋**。C34 暫定模型距後蓋約 3.88 mm。完整數值見 collision_report.json。

## 已補模型與檔案

- 補建 **66 個零件實例**的 STEP 模型（含48個 WS2812、缺失或路徑失效的 IC、插座、開關與模組）。已寫回兩片原始 PCB 的 model 參照，KiCad 可直接讀取。
- 6 組管座陣列，共 **84 個金屬 socket**；6 個暫定方向的 IN-4 機械 STEP 模型也掛到 N1–N6。
- 缺失零件以封裝 F.Fab / B.Fab 或封裝名稱尺寸補形；module、switch 與未知高度是簡化外形，不是供應商認證的精確零件 CAD。標誌、net tie、裸 test pad、固定孔不添加虛構實體。
- DFPlayer 採原廠20×20 mm板外形；高度、插針及內部器件簡化。細節及依據逐項記於 model_specs.json。
- **clock_with_boards.blend**：完整疊合場景，含原始外殼；不是分解圖位置。
- **assembled_45deg.png / internal_45deg.png / exploded.png / socket_detail.png**：整機、去殼、分解與管座細節。
- **models/**：可獨立使用的 STEP 模型；socket_photo_estimate.step 為單個 socket。
- **main_board_before_models.kicad_pcb / display_board_in4_before_models.kicad_pcb**：本次變更前備份。
- validation_checks.json：確認所有非 model 節點相同、所有 model 路徑存在。KiCad 已重新匯出兩片 GLB 驗證可讀取。

## 重建

先运行 inspect_boards.py 與 fit_pins.py（若PCB已補模型，先保留原始 boards.json 供來源差異追蹤）；prepare_models.py 掃描缺失模型；以 FreeCAD Python 執行 build_models.py、build_tube_step.py，再以 attach_models.py 加入模型參照。已補完後不要重跑 prepare_models.py 覆寫原始補件清單。

使用 kicad-cli pcb export glb --force --include-pads 匯出 main_completed.glb / display_completed.glb，然後 Blender --background --python build_assembly.py。最後執行 write_report.py 更新檢查報告。

## 來源

- [IN-4 粗網規格、腳位功能與底視說明](https://www.tube-tester.com/sites/nixie/data/in-4/in-4-sh2.htm)
- [RSH31 底視機械腳位圖](https://www.tube-tester.com/sites/nixie/data/soc/PL31-P/rsh31.gif)
- [DFRobot DFPlayer Mini 外形規格](https://wiki.dfrobot.com/dfr0299)
- [Omnixie NCH8200HV 官方資料](https://nixieclock.org/wp-content/uploads/2023/02/NCH8200HV-Datasheet-EN-v2.1.0.3.pdf)（未據其取得本次實物安裝高度）
- 本專案兩片 kicad_pcb、OUTBOXX_B/F.STL、IN4_nominal 模型，以及使用者 IMG_4339 / 4332–4336 照片。

## 完整 STL / FreeCAD 組裝匯出

- `clock_assembly.stl`：完整組裝網格，含兩片 PCB 與器件、兩件外殼、六支 IN-4 及84個socket。座標以mm輸出，匯入時選毫米。
- `stl_parts/`：16個分件STL（兩件外殼、兩片帶零件PCB、六管、六組socket），全部保留共同組裝座標，可在FreeCAD中分別匯入與移動。
- `clock_assembly.FCStd`：保留相同組裝的FreeCAD原生檔。
- STL 不保存材質、透明度、零件階層或參數；完整STL為多個零件的網格集合，未做布林融合，並非單一可列印實體。
- 延續前述socket尺寸、軸向疊高及字形方向的暫定假設。
- `export_stl_assembly.py` 可從FCStd重建；`stl_export_validation.json` 記錄尺寸與分件檢查。
- 已依要求移除前一步驟的完整STEP及兩片PCB的中間STEP；models/中的既有STEP是KiCad所需零件模型，保留不動。
