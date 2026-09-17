# Revision L — 中央導光柱連牆補強

以 K 版為基礎，在紅圈中央導光柱與按鈕側牆之間新增一片實心補強肋，寬 2.4 mm，與四個螺絲柱的補強肋同寬。肋板由前殼底部接起，頂端停在 Z=31.55，避開上方長按鈕的活動槽。新增幾何範圍 X=123.9～126.3、Y=2.3～6.0、Z=2.3～31.55 mm；中央柱內孔與壓克力棒空間保持暢通。

CAD 與列印網格檢查見 validation.json、mesh_checks.json、print_mesh_checks.json。檢查包含單一有效實體、孔道無新增阻塞、按鈕 0/0.3/0.6 mm 活動、向外防脫與開殼取出。

前後殼、三顆按鈕、托架沿用 K 版列印方向；print_ready 內含六件 STL 與六件毫米單位 3MF。3MF 為通用模型，不含機台設定。只有前殼幾何新增補強，其餘零件維持 K 版。

完整裝配：clock_case_revision_l.blend；CAD：case_revision_l.FCStd；六件 STEP：case_revision_l_print_parts.step。STL／3MF 套件：case_revision_l_PLA_print.zip。

未進行實物 PLA 試印或強度測試。列印平台需容納 250 mm 外殼與約 260 mm 托架，局部懸空與支撐仍需切片確認。CNC STEP 未包含木材、刀具或刀路驗證。根目錄 STL 為裝配座標；列印請使用 print_ready。
