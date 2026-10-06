# Revision L — 中央導光柱連牆補強

以 K 版為基礎，在中央導光柱與按鈕側牆之間新增寬 2.4 mm 的實心補強肋。原設計記錄的幾何範圍為 X=123.9～126.3、Y=2.3～6.0、Z=2.3～31.55 mm。

## 保留檔案

- `clock_case_revision_l.blend`：Blender 裝配場景。
- `case_revision_l_print_parts.step`：六件零件的 STEP 交換檔，可供 CAD 軟體開啟。
- `case_revision_l_PLA_print.zip`：列印方向的六件 STL 與六件毫米單位 3MF，涵蓋前後殼、三顆按鈕與傾斜底座。3MF 不含機台設定。
- `nixie_case_and_button/`：另一組十件獨立 STL，包含四個 PCB 墊片。與 PLA 套件並非逐位元相同，底座的網格大小也不同，因此保留這組模型，不宣稱兩者可互換。

需要列印方向的模型時，從專案根目錄只解壓套件中的 `print_ready/`，避免 ZIP 內的舊 README 覆蓋本說明：

```sh
unzip hardware/case/revision_l/case_revision_l_PLA_print.zip 'print_ready/*' -d hardware/case/revision_l
```

解壓目錄已由 Git 忽略。切片前確認尺寸、方向與支撐；原設計記錄要求平台可容納約 250 mm 外殼與 260 mm 底座，尚未有實物 PLA 試印或強度測試的確認。

## 整理紀錄與驗證範圍

移除根目錄重複的 `front_shell.stl`（與十件零件目錄內版本完全相同）、`nixie_case_and_button.zip`（所有 STL 與已解壓檔案完全相同），以及供裝配預覽的合併網格 `case_revision_l_all_parts.stl`；裝配保留 Blender 場景，列印保留獨立零件。

本分支沒有原生 FreeCAD 檔 `case_revision_l.FCStd`、`validation.json`、`mesh_checks.json` 或 `print_mesh_checks.json`。ZIP 內附的歷史 README 提及這些檔案，但不代表它們目前可用。本次僅整理檔案並核對重複內容，未重新驗證幾何、按鈕行程或製造可行性。
