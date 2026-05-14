# M5Stack Core2 + 秋月 GT-505GGBL5-DR-N GNSS モニタ

## 概要
`M5Stack Core2` と秋月電子の GNSS モジュール `GT-505GGBL5-DR-N` を UART 接続し、NMEA 文を解析して画面と Serial Monitor に表示する `PlatformIO` プロジェクトです。

このプロジェクトでは、単なる緯度経度だけでなく、次の情報を確認できます。

- 測位状態
- 緯度 / 経度 / 高度
- `UsedGGA`
- `UsedGSA`
- `Visible`
- `GSVSeen`
- `HDOP`, `PDOP`, `VDOP`
- 衛星ごとの `SYS / ID / USE / CNO / EL / AZ`

## 対象製品
- 製品名: `GPS衛星受信シリアル出力タイプ(バラ) L1+L5デュアルバンドアンテナ内蔵 1PPS出力付`
- 型番: `GT-505GGBL5-DR-N`
- 秋月電子 商品ページ:
  - https://akizukidenshi.com/catalog/g/g130234/

商品ページから仕様や資料をたどれるようにしてあり、README には接続と使い方を中心にまとめています。

## 配線
秋月 GNSS モジュールの配線色と、`M5Stack Core2` 側の接続先は次のとおりです。

- `赤` -> `VCC`
- `黒` -> `GND`
- `橙 (TXD)` -> `Core2 RX(GPIO13)`
- `緑 (RXD)` -> `Core2 TX(GPIO14)`
- `茶 (PPS)` -> 今回は未使用

## UART 設定
現在の動作確認設定です。

- `Serial2`
- `RX = GPIO13`
- `TX = GPIO14`
- `115200 bps`

## 解析している NMEA 文
- `GGA`
- `GSA`
- `GSV`
- `RMC`

## 表示値の意味
- `UsedGGA`
  - `GGA` に出てくる使用衛星数
- `UsedGSA`
  - `GSA` に列挙された、実際に測位計算へ使っている衛星 ID 数
- `Visible`
  - 表示ロジックで採用している可視衛星数
- `GSVSeen`
  - `GSV` が報告している見えている衛星数

秋月 GNSS では、観測上 `UsedGGA` と `UsedGSA` が一致しない場合があります。そのため、このプロジェクトでは両方を分けて表示しています。

## ビルド
```powershell
platformio run
platformio run --target upload
platformio device monitor -b 115200
```

## 補足メモ
秋月 GNSS の観測メモや解析メモは以下にまとめています。

- [AKIZUKI_GNSS_NOTES.md](./AKIZUKI_GNSS_NOTES.md)
