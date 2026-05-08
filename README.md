# Sesami Lock MitoLab

自動施錠・解錠スケジューラー — Seeed XIAO ESP32C6 + DS1302 RTC + SESAME スマートロック

[English](README.en.md)

---

> **免責事項**: このプロジェクトのコード・ドキュメントはすべて AI (Claude) によって生成されています。動作保証はなく、作者は一切の責任を負いません。自己責任でご使用ください。

---

## 概要

設定した時刻に SESAME スマートロックを自動で施錠・解錠します。DS1302 RTC で正確な時刻を保持し、ESP32C6 のディープスリープを活用して低消費電力を実現します。起動時にシリアルシェルが立ち上がり、時刻やスケジュールをその場で設定できます。

---

## ハードウェア

| 部品 | 型番 |
| --- | --- |
| マイコン | Seeed Studio XIAO ESP32C6 |
| RTC | DS1302 (+ 32.768 kHz 水晶) |
| スマートロック | SESAME 3/4/5/5 PRO など |

---

## 回路図

```text
                    XIAO ESP32C6
                  ┌─────────────┐
              D0  │●           ●│ 5V
              D1  │●           ●│ GND
              D2  │●           ●│ 3V3
              D3  │●           ●│ D10 ──── CLK (SCL)
              D4  │●           ●│ D9  ──── DAT (IO)
              D5  │●           ●│ D8  ──── RST
              D6  │●           ●│ D7
                  └─────────────┘
                                             DS1302
                                          ┌──────────┐
                              3V3 ────── │ VCC      │
                              GND ────── │ GND      │
                        CLK (D10) ────── │ CLK      │
                         DAT (D9) ────── │ DAT      │
                         RST (D8) ────── │ RST      │
                                         │ X1 ──┐   │
                                         │ X2 ──┤   │  32.768 kHz
                                         └──────┼───┘  水晶発振子
                                               ═╧═
```

### 配線まとめ

| XIAO ピン | DS1302 ピン | 説明 |
| --- | --- | --- |
| D8 | RST | チップセレクト |
| D9 | DAT (IO) | シリアルデータ |
| D10 | CLK (SCL) | クロック |
| 3V3 | VCC | 電源 3.3 V |
| GND | GND | グランド |

> SESAME ロックとの通信は Bluetooth LE (BLE) 経由のため、追加配線は不要です。

---

## 動作フロー

```text
起動
  │
  ├─[初回起動]──→ シリアルシェル (10 秒待機)
  │                  ↓ "run" または タイムアウト
  └─[スリープ復帰]
        │
        ↓
    RTC で現在時刻を確認
        │
        ├─[解錠時刻を過ぎていて未解錠]
        │     → BLE スキャン → 接続 → unlock()
        │
        ├─[施錠時刻を過ぎていて未施錠]
        │     → BLE スキャン → 接続 → lock()
        │
        └─[まだ時刻でない]
              → 次のイベントまでディープスリープ (最大 1 時間)
```

---

## 環境構築

### 必要なもの

- [VS Code](https://code.visualstudio.com/)
- [PlatformIO IDE 拡張機能](https://marketplace.visualstudio.com/items?itemName=platformio.platformio-ide)
- USB-C ケーブル (XIAO との接続用)

### セットアップ手順

1. VS Code に **PlatformIO IDE** 拡張機能をインストール
2. このリポジトリをクローン

   ```bash
   git clone https://github.com/zhixuan2333/sesami-lock-auto-lock-unlock.git
   cd sesami-lock-auto-lock-unlock
   ```

3. VS Code でフォルダを開く → PlatformIO が自動でライブラリを取得
4. Sesame キー情報を設定

   ```bash
   cp src/mysesame-config.h.example src/mysesame-config.h
   # SESAME_SECRET / SESAME_PK / SESAME_MODEL を編集
   ```

5. PlatformIO のサイドバーから **Upload** → ビルド & 書き込み
6. **Serial Monitor** でシリアル出力を確認 (115200 baud)

<details>
<summary>CLI (uv + pio) でセットアップする場合</summary>

```bash
# uv のインストール
# macOS / Linux
curl -LsSf https://astral.sh/uv/install.sh | sh
# Windows (PowerShell)
powershell -ExecutionPolicy ByPass -c "irm https://astral.sh/uv/install.ps1 | iex"

# 仮想環境 & PlatformIO
uv venv
uv pip install platformio

# よく使うコマンド
uv run pio run                   # ビルド
uv run pio run --target upload   # 書き込み
uv run pio device monitor        # シリアルモニタ
uv run pio pkg update            # ライブラリの更新
```

</details>

---

## SESAME キーの取得

1. SESAME アプリでデバイスの QR コードを表示する
2. [https://sesame-qr-reader.vercel.app/](https://sesame-qr-reader.vercel.app/) で QR を読み取る
3. 表示された `Secret Key` と `Public Key` を `mysesame-config.h` に設定

```cpp
// src/mysesame-config.h
#define SESAME_SECRET "your_secret_key_here"
#define SESAME_PK     "your_public_key_here"
#define SESAME_MODEL  Sesame::model_t::sesame_5_pro  // 使用機種に合わせて変更
```

---

## シリアルシェルのコマンド

起動後 10 秒間、シリアルモニタでコマンドを入力できます (115200 baud)。

```text
> help
Commands:
  time                         - show current RTC time
  time set YYYY-MM-DD HH:MM:SS - set RTC time
  schedule                     - show unlock/lock schedule
  unlock HH:MM                 - set daily unlock time (default 08:00)
  lock HH:MM                   - set daily lock time   (default 20:00)
  reset                        - reset today's action flags
  run                          - exit shell and start operation
```

### 例

```text
> time set 2026-05-08 09:00:00
> unlock 07:30
> lock 22:00
> run
```

---

## set_rtc_time.py — RTC 時刻自動設定スクリプト

PC の現在時刻を RTC に書き込む Python スクリプトです。
[uv](https://github.com/astral-sh/uv) を使えば `pyserial` を事前インストールせず、そのまま実行できます。

### インストール不要で即実行

```bash
# Windows (PowerShell)
powershell -ExecutionPolicy ByPass -c "irm https://astral.sh/uv/install.ps1 | iex"

# macOS / Linux
curl -LsSf https://astral.sh/uv/install.sh | sh
```

### 使い方

```bash
# 時刻だけ合わせる
uv run set_rtc_time.py COM3          # Windows
uv run set_rtc_time.py /dev/ttyUSB0  # Linux / macOS

# 時刻 + unlock/lock スケジュールも設定
uv run set_rtc_time.py COM3 --unlock 08:00 --lock 22:00
```

### オプション

| オプション | 説明 | デフォルト |
| --- | --- | --- |
| `port` | シリアルポート (必須) | — |
| `--baud` | ボーレート | `115200` |
| `--unlock HH:MM` | 毎日の解錠時刻 | 変更しない |
| `--lock HH:MM` | 毎日の施錠時刻 | 変更しない |

> スクリプトは起動後 8 秒以内にシリアルシェルを検出し、`time set`・`unlock`・`lock`・`run` コマンドを自動送信します。

---

## 対応 SESAME モデル

`SESAME_MODEL` に指定できる値:

| 値 | 機種 |
| --- | --- |
| `sesame_3` | SESAME 3 |
| `sesame_4` | SESAME 4 |
| `sesame_5` | SESAME 5 |
| `sesame_5_pro` | SESAME 5 PRO |
| `sesame_6` | SESAME 6 |
| `sesame_6_pro` | SESAME 6 PRO |
| `sesame_bot` | SESAME bot |
| `sesame_bot_2` | SESAME Bot 2 |

---

## 依存ライブラリ

| ライブラリ | バージョン | 用途 |
| --- | --- | --- |
| [libsesame3bt](https://github.com/homy-newfs8/libsesame3bt) | 0.32.0 | SESAME BLE 通信 |
| [makuna/RTC](https://github.com/Makuna/Rtc) | ^2.4.2 | DS1302 RTC 制御 |

---

## ライセンス

MIT
