# CH559 USB Mouse Host

[English](README.md) · **日本語**

WCH **CH559** を USB ホストにして USB マウスを列挙し、HID レポートをパースして
ボタン / X / Y / ホイール / 水平ホイール(Consumer AC Pan) を取得、**SPI0 スレーブ**で
出力しつつ **UART0** に人間可読のデバッグログを出すファームウェアです。

![CH559 test setup](images/ch559_dev_setup.jpg)

> **ZMK キーボードで使う**: このファームが SPI 出力するマウス状態を ZMK 側で受け取る
> 入力モジュールが [zmk-ch559-spi](https://github.com/tadakado/zmk-ch559-spi) です。
> （無線キーボードで USB マウスをポインティングデバイスとして使えます）

## ライセンス / 帰属 (License & Attribution)

本プロジェクトは **GNU General Public License v3.0 (GPL-3.0-only)** で配布します。

これは派生元である
[atc1441/CH559sdccUSBHost](https://github.com/atc1441/CH559sdccUSBHost)（**GPLv3**）の
ペリフェラル/USB ホスト列挙コードを取り込み・改変しているためです。`src/` のうち
`CH559.h` / `util.{c,h}` / `uart.{c,h}` / `USBHost.{c,h}` は同リポジトリ由来（`USBHost.c`
は本プロジェクト向けに改変）で、それらとリンクする本ファーム全体が GPLv3 の派生物に
なります。各ファイル先頭の SPDX ヘッダに出所を記載しています。

- 全文: [`LICENSE`](LICENSE)（GPLv3）
- 原作: atc1441 — https://github.com/atc1441/CH559sdccUSBHost
- `CH559.h` のレジスタ定義は WCH 公式由来で、atc1441 が GPLv3 として再配布したものです。

> 注: `hid_parser.{c,h}` は GPL コードに依存しない独立実装（ハード非依存）ですが、本リポ
> ジトリの結合著作物の一部としては GPLv3 で配布されます。

## 構成

| パス | 役割 |
|---|---|
| `src/hid_parser.{c,h}` | HID レポート記述子パーサ（自作・ホストテスト可） |
| `src/spi_out.{c,h}` | SPI0 スレーブ出力（自作） |
| `src/main.c` | エントリポイント（自作） |
| `src/USBHost.{c,h}`, `util.*`, `uart.*`, `CH559.h` | atc1441 由来（列挙/ポーリング/ペリフェラル） |
| `test/test_hid_parser.c` | ホスト側ユニットテスト |
| `tools/serial_monitor.py` | UART モニタ（ポート自動検出） |
| `tools/ch559_spi_check/` | Arduino Nano 33 BLE(nRF52840) 用 SPI 受信チェックスケッチ |
| `tools/ch559_spi_check_xiao/` | Seeed XIAO nRF52840 Plus 用 SPI 受信チェックスケッチ |
| `tools/ch559_spi_check_burst/` | 読み出しを絞ってボタン押下エッジを数える検証スケッチ（クリック取りこぼし確認用） |

詳細仕様は [`SPEC.ja.md`](SPEC.ja.md) を参照。

## ピン割り当て（CH559, 3.3V）

| 機能 | CH559 ピン | 方向 | 備考 |
|---|---|---|---|
| デバッグ UART0 TXD | **P0.2** | 出力 | 代替ピン（`initUART0(baud, 1)`）。115200 baud |
| デバッグ UART0 RXD | **P0.3** | 入力 | 代替ピン。`kb\n` 受信で ISP（ブートローダー）移行 |
| SPI0 SCK | **P1.7** | 入力 | マスタからのクロック |
| SPI0 MISO | **P1.6** | 出力 | フレーム出力（チップセレクト時に自動駆動） |
| SPI0 MOSI | **P1.5** | 入力 | 本用途では未使用 |
| SPI0 SCS / CS | **P1.4** | 入力 | チップセレクト（アクティブ Low） |
| DRDY | **P1.3** | 出力 | データレディ（アクティブ Low） |
| DOWNLOAD / boot ボタン | **P4.6** | 入力 | Low でブートローダー起動（ISP） |
| USB ホスト | ルートポート ×2（HUB0/HUB1） | — | 内蔵 USB-PHY。直挿しのみ（外付けハブ非対応） |

SPI マスタ（nRF52840 等）との接続は 3.3V 直結。Arduino Nano 33 BLE / Seeed XIAO
nRF52840 Plus での配線例（3ボード対応のピン対応表）は [`SPEC.ja.md`](SPEC.ja.md) §6 を参照。

## ビルド / 書き込み / テスト

ツールチェーン: SDCC 4.5 / wchisp 0.3 / uv（pyserial）/ GNU Make（macOS で確認）。

```bash
make                       # ビルド（生成物は build/、UART デバッグ出力あり）
make clean && make RELEASE=1   # 本番ビルド: UART デバッグ出力を全て除去
make clean && make RAW=1   # デバッグビルド: UART に生レポートの RAW hex ダンプを追加
make test                  # hid_parser のホストユニットテスト
make flash                 # wchisp で書き込み（要 ISP モード）
uv run --with pyserial tools/serial_monitor.py   # UART モニタ（ポート自動検出, 115200）
```

UART 出力は既定で有効（`M b=.. dx=.. dy=.. w=.. hw=..` 等）。`RAW=1` では各行頭に
`RAW len=.. <hex>` が付く。**`RELEASE=1` では毎レポートの UART デバッグ出力をコンパイル時に
全削除**する（その `printf` は 115200 baud で ~2〜3ms 主ループをブロックするため、本番では切る）。
ただし**起動バナー（下記）はどのビルドでも出力**する（一度きりで負荷がなく、生存確認・
ボーレート確認・ビルド判別に使えるため）。SPI 出力と `kb` による ISP 移行も RELEASE で有効。
`RELEASE`/`RAW` の切替時は `make clean` が必要。

起動時に UART0 へ次のバナーを出す（`__DATE__`/`__TIME__` と `DEBUG`/`RAW`/`RELEASE` 種別入り）:

```
=== CH559 USB Mouse Host ===
build Jun 11 2026 09:40:12 [DEBUG]  uart 115200 8N1  SPI0 slave 10B
ready
```

ISP（ブートローダー）への入り方は `SPEC.ja.md` §2、検証済み構成は §6 を参照。

## SPI 出力（マスタ = nRF52840 / ZMK 想定）

10バイトフレーム:
```
[0xAA][btnL][btnH][dxL][dxH][dyL][dyH][wheel][hwheel][XOR]
```
- `buttons`,`dx`,`dy`: 16bit リトルエンディアン（dx/dyは符号付き、buttonsは最大16個）/ `wheel`,`hwheel`: 符号付き 8bit
- `XOR` = 先頭からの総 XOR（byte0..byte8）

特性:
- **割り込み駆動・非ブロッキング**（`bS0_IE_BYTE`）。SPI0 スレーブ, mode0 / MSB first。
- **デルタ累積**: dx/dy/wheel/hwheel は読まれるまで飽和加算、buttons は最新（取りこぼし防止）。
- **DRDY（P1.3, アクティブLow）**: 未読フレームありでアサート。再アームはマスタが
  **CS を High に戻した後**に行うので、1回の読み取り中にフレームは変化しない。

マスタ側プロトコル（ZMKカスタム入力ドライバの指針）:
1. DRDY=Low を検出 → CS を Low。
2. **10バイト読む** → `byte0 == 0xAA`（同期）と XOR 一致を確認 → デコード。
   先頭バイトはスレーブの `SPI0_S_PRE` から出る同期バイト(0xAA)なので整列済み（破棄/再同期は不要）。
3. CS を High に戻す。新データがあれば DRDY が再び Low になる。

検証（実機確認済み, 3.3V 直結）:
- `tools/ch559_spi_check/`（Arduino Nano 33 BLE = nRF52840）および
  `tools/ch559_spi_check_xiao/`（Seeed XIAO nRF52840 Plus）を SPI マスタにし、`arduino-cli`
  で書き込んで DRDY 連動の 10 バイト読み出しを行い、移動・ボタン・縦横ホイールの一致を確認。
  ピン接続は [`SPEC.ja.md`](SPEC.ja.md) §6 を参照。
- `tools/ch559_spi_check_burst/`（Nano 33 BLE）は読み出しを 150ms に絞り、押下エッジ
  （bit 0→1）を数える検証スケッチ。読み出しの合間に完結した単発クリックが取りこぼされず
  届くこと（押下フレーム→解放フレーム）を確認できる。配線は `ch559_spi_check` と同じ。
