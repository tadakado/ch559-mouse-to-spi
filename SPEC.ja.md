# CH559 USB マウスホスト — 仕様 / 統合ガイド

[English](SPEC.md) · **日本語**

このファームを**焼いて・配線して・SPI マスタ側（nRF52840 / ZMK 等）で受け取る**ために
必要な情報をまとめる。概要は [README](README.ja.md) を参照。

## 1. ハードウェア

- **MCU**: WCH CH559（8051 系, Code Flash 60 KiB, Data EEPROM 1 KiB, BTVER 2.40）
- **クロック**: 48 MHz（内蔵 12 MHz OSC + PLL）
- **USB**: コントローラ 1 個 + 物理ポート 2 個（HUB0/HUB1）のデュアルルートハブ。
  **物理 2 ポートへの直挿しのみ**列挙する（外付け USB ハブ配下は非対応）。
- **デバッグ UART**: UART0 代替ピン（TXD=P0.2, RXD=P0.3, `initUART0(baud, 1)`）、115200 baud。
- **SPI 出力**: SPI0 スレーブ（SCK=P1.7, MISO=P1.6, MOSI=P1.5, SCS=P1.4, DRDY=P1.3）。

### ピン配

| 機能 | CH559 ピン | 方向 | 備考 |
|---|---|---|---|
| UART0 TXD | **P0.2** | 出力 | 115200 baud（代替ピン）|
| UART0 RXD | **P0.3** | 入力 | `kb\n` 受信で ISP 移行 |
| SPI0 SCK | **P1.7** | 入力 | マスタからのクロック |
| SPI0 MISO | **P1.6** | 出力 | フレーム出力（CS 時に自動駆動）|
| SPI0 MOSI | **P1.5** | 入力 | 本用途では未使用 |
| SPI0 SCS / CS | **P1.4** | 入力 | チップセレクト（アクティブ Low）|
| DRDY | **P1.3** | 出力 | データレディ（アクティブ Low）|
| DOWNLOAD / boot | **P4.6** | 入力 | Low でブートローダー起動（ISP）|
| USB ホスト | ルートポート ×2 | — | 内蔵 PHY・直挿しのみ |

## 2. ISP（ブートローダー）移行と書き込み

次のいずれかで ISP に入り、`wchisp flash build/ch559mouse.hex` で書き込む。

- **(a) DOWNLOAD(P4.6)=Low のままリセット** — チップがブートローダー起動（アプリ非依存）。初回・復旧はこれ。
- **(b) UART に `kb\n` を送る** — 動作中ファームが `runBootloader()`。以降は PC 側から ISP 再突入を制御でき書き込みを自動化できる。
- **(c) 動作中に P4.6 を Low** — メインループが検出して `runBootloader()`。

`wchisp` は USB 直結（VID:PID 4348:55e0）でシリアルとは別経路のため、screen 等と同時使用可。

## 3. ツールチェーン (macOS)

- **SDCC** 4.5.0（`brew install sdcc`, `mcs51`）
- **wchisp** 0.3.0（`.hex` 直接書き込み、`.bin`/objcopy 不要）
- **uv**（`uv run --with pyserial tools/serial_monitor.py`）
- **GNU Make**

ビルドコマンド（`make` / `make RELEASE=1` / `make RAW=1` / `make test` / `make flash`）は
[README](README.ja.md) を参照。

## 4. SPI 出力（マスタ = nRF52840 / ZMK 想定）

**10 バイトフレーム**:
```
[0xAA][btnL][btnH][dxL][dxH][dyL][dyH][wheel][hwheel][XOR]
```
- `buttons`,`dx`,`dy`: 16bit リトルエンディアン（`dx`/`dy` は符号付き・フル分解能、`buttons` は最大 16 個）
- `wheel`,`hwheel`: 符号付き 8bit
- `XOR` = `byte0 ^ … ^ byte8`

**特性**:
- **割り込み駆動・非ブロッキング**（`bS0_IE_BYTE`）。SPI0 スレーブ, mode0 / MSB first。
- **デルタ累積**: `dx`/`dy`/`wheel`/`hwheel` はマスタが読むまで飽和加算。`buttons` は最新状態に
  「前フレーム以降に一度でも押された bit」を OR してエッジをラッチするため、読み取りの合間に
  完結した単発クリックも取りこぼさない（押下フレーム→解放フレームの順で配信）。
- **DRDY（P1.3, アクティブ Low）**: 未読フレームありでアサート。再アームはマスタが **CS を High に
  戻した後**に行うため、1 回の読み取り中にフレームは変化しない。
- **同期バイト**: CS 後の最初の 1 バイトはプリセットレジスタ `SPI0_S_PRE`（=0xAA）から出るため
  先頭は常に整列している（マスタ側で破棄・再同期は不要）。

**マスタ側プロトコル**:
1. DRDY=Low を検出 → CS を Low。
2. **10 バイト読む** → `byte0 == 0xAA` と XOR 一致を確認 → デコード。
3. CS を High に戻す。新データがあれば DRDY が再び Low になる。

## 5. 対応マウス / HID

HID Report Descriptor をパースして buttons / X(0x30) / Y(0x31) / wheel(0x38) /
水平ホイール(Consumer AC Pan 0x0238) のビット位置と Report ID を抽出する。Report ID 対応。
記述子準拠の通常マウスが対象（ブートプロトコル専用マウス向けフォールバックは未実装）。

検証済み: Logitech レシーバ VID:0x046D / PID:0xC548（Report ID 2、9 バイト/レポート）:
```
[ID=2][buttons16(byte1-2)][X16(byte3-4)][Y16(byte5-6)][wheel8(byte7)][hwheel8(byte8)]
```

## 6. 実証済み構成 / 配線例

nRF52840 系（ZMK と同チップ）を SPI マスタにして、3.3V 直結・GND 共通で検証済み。

| 信号 | CH559 | Arduino Nano 33 BLE | Seeed XIAO nRF52840 Plus |
|---|---|---|---|
| SCK | P1.7 | D13 | D17 (P1.03) |
| MISO | P1.6 | D12 | D18 (P1.05) |
| MOSI | P1.5 | D11 | D19 (P1.07) |
| SCS | P1.4 | D10 | D15 (P0.10, NFC2) |
| DRDY | P1.3 | D7 | D14 (P0.09, NFC1) |
| GND | GND | GND | GND |

SPI 受信チェックスケッチ: `tools/ch559_spi_check/`（Nano 33 BLE）/
`tools/ch559_spi_check_xiao/`（XIAO nRF52840 Plus）。`arduino-cli` で書き込む。

**XIAO の注意（NFC ピン）**: SCS(D15=P0.10) と DRDY(D14=P0.09) は nRF52840 の NFC アンテナピン。
Seeed コアは既定で NFC モードのままなので `digitalRead` が実レベルを返さず DRDY ゲートが効かない。
`-DCONFIG_NFCT_PINS_AS_GPIOS` を付けてビルドすると `UICR.NFCPINS` が一度だけ書き換わり（恒久）、
通常 GPIO になる。`arduino-cli` では
`--build-property compiler.c.extra_flags=-DCONFIG_NFCT_PINS_AS_GPIOS` で渡し、`compile --upload`
で書き込む（`upload` は `--build-property` 非対応）。XIAO のカスタム SPI は
`SPIClass(NRF_SPIM2, MISO, SCK, MOSI)`（Seeed コアの引数順）。

## 7. 既知の制限

- 外付け USB ハブは非対応（物理 2 ポート直挿しのみ）。
- ブートプロトコル専用マウス向けフォールバックは未実装（記述子準拠の通常マウスは可）。
- SPI 出力の分解能: `dx`/`dy`・`buttons` は 16bit、`wheel`/`hwheel` は 8bit。

## 8. 消費電力

CH559 は USB ホスト動作中、USB PHY + 48MHz コアが常時稼働し消費電力の下限が高い。実用的な削減策は
2 つで、いずれも動作・操作感を変えずに適用済み:

- **ポーリングの `bInterval` ペーシング**: 各エンドポイントの `bInterval`(ms) 間隔でだけ IN を発行し、
  レポートが無い時間の IN/NAK 連打（バス・デバイス電力の浪費）を止める。
- **クロックゲーティング**: 未使用ペリフェラル（ADC/UART1/PWM1・SPI1/Timer3/LED）のクロックを
  `SLEEP_CTRL` で停止（USB/SPI0/Timer0,1/UART0/XRAM は維持）。

PD スリープは、ホストモードでは PHY 停止によりデバイス装着で復帰できず、この設計では実用的な
適用先が無い。
