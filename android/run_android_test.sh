#!/bin/bash
# STK Android 측정 자동 실행 + 결과를 PC 폴더로 자동 저장
#
# 사용법 (Git Bash):
#   ./run_android_test.sh <CSV 접두어> <PC 저장 폴더> <판 수>
# 예시:
#   ./run_android_test.sh item_latency  /c/logs/sit1_android 10
#   ./run_android_test.sh wall_recovery /c/logs/sit2_android 10
#   ./run_android_test.sh fall_respawn  /c/logs/sit3_android 10
#   ./run_android_test.sh banana_debuff /c/logs/sit4_android 10
#
# 폰의 config.xml commandline 값은 측정 항목에 맞게 미리 넣어 두어야 합니다.

PREFIX="$1"
OUT_DIR="$2"
RUNS="${3:-10}"

# ---- 필요하면 수정 -------------------------------------------------------
ADB_CMD="${ADB:-adb}"                     # $ADB가 없으면 adb 사용
PKG="org.supertuxkart.stk"                # 앱 패키지 이름
PHONE_DIR="/storage/emulated/0/Android/data/org.supertuxkart.stk/files/supertuxkart/home/supertuxkart/config-0.10"   # 폰의 config-0.10 폴더 (비우면 자동 검색)
# -------------------------------------------------------------------------

if [ -z "$PREFIX" ] || [ -z "$OUT_DIR" ]; then
    echo "사용법: $0 <CSV 접두어> <PC 저장 폴더> [판 수]"
    exit 1
fi

mkdir -p "$OUT_DIR"

# Git Bash는 /storage/... 같은 폰 경로를 윈도우 경로로 바꿔버리므로 변환을 끄고,
# PC 저장 폴더는 adb.exe가 이해하는 C:/... 형식으로 바꿔 둠
export MSYS_NO_PATHCONV=1
if command -v cygpath >/dev/null 2>&1; then
    OUT_DIR=$(cygpath -m "$OUT_DIR")
fi

# 폰의 config-0.10 폴더 찾기
if [ -z "$PHONE_DIR" ]; then
    PHONE_DIR=$("$ADB_CMD" shell "find /sdcard /storage/emulated/0 -type d -name config-0.10 2>/dev/null" \
                | tr -d '\r' | head -n 1)
fi
if [ -z "$PHONE_DIR" ]; then
    echo "폰에서 config-0.10 폴더를 찾지 못했습니다. 스크립트의 PHONE_DIR에 직접 넣어 주세요."
    exit 1
fi
echo "폰 결과 폴더: $PHONE_DIR"
echo "PC 저장 폴더: $OUT_DIR"

# 이전에 남아 있던 같은 종류의 CSV는 폰에서 치워 둠 (PC로 먼저 백업)
for f in $("$ADB_CMD" shell "ls $PHONE_DIR" 2>/dev/null | tr -d '\r' \
           | grep "^${PREFIX}_.*\.csv$" | sed "s|^|$PHONE_DIR/|"); do
    mkdir -p "$OUT_DIR/old_on_phone"
    "$ADB_CMD" pull "$f" "$OUT_DIR/old_on_phone/" >/dev/null
    "$ADB_CMD" shell "rm '$f'"
done

for ((i = 1; i <= RUNS; i++)); do
    echo "=== $i / $RUNS 판 시작 ==="
    "$ADB_CMD" shell am force-stop "$PKG"
    echo "  앱 실행 중..."
    "$ADB_CMD" shell monkey -p "$PKG" -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1

    # 앱이 뜰 때까지 대기
    for ((w = 0; w < 30; w++)); do
        [ -n "$("$ADB_CMD" shell pidof "$PKG" | tr -d '\r')" ] && break
        sleep 1
    done

    # 측정이 끝나 CSV가 생길 때까지 대기 (최대 10분)
    echo "  측정 결과(CSV) 기다리는 중..."
    found=""
    for ((w = 0; w < 600; w++)); do
        found=$("$ADB_CMD" shell "ls $PHONE_DIR" 2>/dev/null | tr -d '\r' \
                | grep "^${PREFIX}_.*\.csv$" | sed "s|^|$PHONE_DIR/|")
        [ -n "$found" ] && break
        sleep 1
    done
    sleep 2   # 파일 쓰기가 끝나도록 잠시 대기
    "$ADB_CMD" shell am force-stop "$PKG"

    if [ -z "$found" ]; then
        echo "  $i 판: CSV가 생기지 않았습니다 (건너뜀)"
        continue
    fi

    # PC로 가져온 뒤 폰에서 삭제 → 이름이 겹쳐 덮어쓰이는 일 없음
    for f in $found; do
        name=$(basename "$f" .csv)
        dest="$OUT_DIR/${name}_run$(printf '%02d' "$i").csv"
        "$ADB_CMD" pull "$f" "$dest" >/dev/null && "$ADB_CMD" shell "rm '$f'"
        echo "  저장: $dest"
    done
done

echo "완료: $OUT_DIR"
