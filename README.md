# M5Stack Core2 + 秋月 GT-505GGBL5-DR-N GNSS モニタ

## 概要

このプロジェクトは、`M5Stack Core2` から秋月電子の GNSS モジュール  
`GT-505GGBL5-DR-N` を `UART` で受信し、NMEA 文を解析して表示するための `PlatformIO` プロジェクトです。

単に緯度・経度を出すだけでなく、以下を確認できるようにしています。

- 現在の測位状態
- 緯度・経度・高度
- DOP 値
- 測位に使われている衛星
- 見えている衛星
- 各衛星の `C/N0`、仰角、方位角

## 対応している NMEA 文

現在のコードでは、以下の文を解析しています。

- `GGA`
- `GSA`
- `GSV`
- `RMC`

役割は次のとおりです。

- `GGA`
  - Fix quality
  - 緯度・経度
  - 使用衛星数
  - HDOP
  - 高度
- `GSA`
  - Fix type
  - 測位に使っている衛星 ID 一覧
  - `PDOP`, `HDOP`, `VDOP`
- `GSV`
  - 見えている衛星一覧
  - 衛星 ID
  - 仰角
  - 方位角
  - `C/N0`
- `RMC`
  - 位置情報の補助
  - 速度

## ハードウェア構成

- Board: `M5Stack Core2`
- GNSS module: `GT-505GGBL5-DR-N`
- UART: `Serial2`
- RX pin: `GPIO13`
- TX pin: `GPIO14`
- 確認できている動作 baudrate: `115200`

## このプロジェクトで見たいこと

このプロジェクトは、次のような GNSS の違いを見るために作成しました。

1. `M5Stack Core2 + 秋月 GT-505GGBL5-DR-N`
2. `M5Stack CoreS3 + M5Stack GNSS Module / u-blox NEO-M9N`

特に重要視しているのは以下です。

- どの衛星が見えているか
- どの衛星が測位に使われているか
- `GGA`, `GSA`, `GSV` の意味の違い

## 現在の表示内容

現在の画面・Serial では主に以下を表示します。

- `Fix`
- `Lat`
- `Lon`
- `Alt`
- `UsedGGA`
- `UsedGSA`
- `Visible`
- `GSVSeen`
- `HDOP`
- `PDOP`
- `VDOP`
- 衛星一覧
  - `SYS`
  - `ID`
  - `USE`
  - `CNO`
  - `EL`
  - `AZ`

## 表示値の意味

### `UsedGGA`

`GGA` の「Satellites Used」欄をそのまま表示した値です。

### `UsedGSA`

`GSA` に実際に列挙されている衛星 ID 数を数えた値です。  
今のところ、こちらのほうが「実際に使っている衛星数」に近い指標として扱いやすいです。

### `Visible`

表示ロジックが最終的に採用した「見えている衛星数」です。

### `GSVSeen`

`GSV` の satellites in view 欄をもとにした衛星数です。

## 現時点で分かっていること

この秋月 GNSS では、`UsedGGA` と `UsedGSA` が一致しないことが確認されています。

観測例:

- `UsedGGA = 41`
- `UsedGSA = 24`
- `Visible = GSVSeen`

このことから、少なくとも現時点では次のように解釈しています。

- `GSV` 側の解析は比較的素直に見えている
- `GGA` の衛星数は、`GSA` の単純な衛星 ID 数とは同じ意味ではない可能性が高い

そのため、衛星使用数を見るときは、まず `UsedGSA` を優先して読むのが実用的です。

## ビルド方法

`PlatformIO` プロジェクトです。

代表的なコマンド:

```powershell
platformio run
platformio run --target upload
platformio device monitor -b 115200
```

## メモ

秋月 GNSS についての調査メモは以下にまとめています。

- [AKIZUKI_GNSS_NOTES.md](./AKIZUKI_GNSS_NOTES.md)

## リポジトリに含めるもの

このリポジトリでは、主に以下を管理します。

- ソースコード
- PlatformIO の設定
- Markdown の調査メモ

`.pio/` などのビルド生成物は含めません。
