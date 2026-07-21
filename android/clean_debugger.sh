#!/usr/bin/env bash

set -u

# 사용법:
#   ./check_stk_debug.sh
#   ./check_stk_debug.sh RF9T101PRKW org.supertuxkart.stk_dbg

SERIAL="${1:-RF9T101PRKW}"
PKG="${2:-org.supertuxkart.stk_dbg}"

# 환경변수 ADB가 지정되어 있으면 우선 사용
if [[ -n "${ADB:-}" ]]; then
    ADB_BIN="$ADB"

# PATH에 adb가 등록되어 있는 경우
elif command -v adb.exe >/dev/null 2>&1; then
    ADB_BIN="$(command -v adb.exe)"
elif command -v adb >/dev/null 2>&1; then
    ADB_BIN="$(command -v adb)"

# WSL 경로
elif [[ -x "/mnt/c/android-sdk/platform-tools/adb.exe" ]]; then
    ADB_BIN="/mnt/c/android-sdk/platform-tools/adb.exe"

# Git Bash 경로
elif [[ -x "/c/android-sdk/platform-tools/adb.exe" ]]; then
    ADB_BIN="/c/android-sdk/platform-tools/adb.exe"

else
    echo "오류: adb를 찾을 수 없습니다."
    echo "ADB 환경변수를 지정하거나 Android SDK 경로를 확인하세요."
    exit 1
fi

adb_device() {
    "$ADB_BIN" -s "$SERIAL" "$@"
}

echo "=================================================="
echo "ADB        : $ADB_BIN"
echo "Device     : $SERIAL"
echo "Package    : $PKG"
echo "=================================================="

# 장치 연결 상태 확인
if ! adb_device get-state >/dev/null 2>&1; then
    echo
    echo "오류: 장치 '$SERIAL'에 연결할 수 없습니다."
    echo
    "$ADB_BIN" devices -l
    exit 1
fi

echo
echo "=== STK 앱 프로세스 ==="

PID="$(
    adb_device shell pidof "$PKG" 2>/dev/null |
        tr -d '\r' || true
)"

if [[ -n "$PID" ]]; then
    echo "PID: $PID"
else
    echo "실행 중인 프로세스가 없습니다."
fi

echo
echo "=== 기기의 LLDB / STK 관련 프로세스 ==="

DEVICE_PS="$(
    adb_device shell \
        'ps -A -o USER,PID,PPID,STAT,NAME,ARGS 2>/dev/null || ps -A' \
        2>/dev/null |
        tr -d '\r' || true
)"

if ! printf '%s\n' "$DEVICE_PS" |
    grep -Ei 'supertuxkart|lldb|gdbserver|start_lldb'; then
    echo "관련 프로세스가 없습니다."
fi

echo
echo "=== ADB 포워드 ==="

FORWARDS="$(
    "$ADB_BIN" forward --list 2>/dev/null |
        tr -d '\r' |
        grep -F "$SERIAL" || true
)"

if [[ -n "$FORWARDS" ]]; then
    printf '%s\n' "$FORWARDS"
else
    echo "이 장치에 등록된 포워드가 없습니다."
fi

echo
echo "=== Windows 측 LLDB 프로세스 ==="

# Git Bash 또는 WSL에서 Windows PowerShell 호출
if command -v powershell.exe >/dev/null 2>&1; then
    powershell.exe -NoProfile -Command '
        [Console]::OutputEncoding = [System.Text.Encoding]::UTF8

        $names = @(
            "LLDBFrontend.exe",
            "lldb.exe",
            "lldb-server.exe"
        )

        $processes = Get-CimInstance Win32_Process |
            Where-Object {
                $names -contains $_.Name
            } |
            Select-Object ProcessId, ParentProcessId, Name, CommandLine

        if ($processes) {
            $processes | Format-Table -AutoSize
        }
        else {
            Write-Output "관련 Windows 프로세스가 없습니다."
        }
    '
else
    echo "powershell.exe를 찾을 수 없습니다."
    echo "현재 Linux 환경의 LLDB 프로세스만 조회합니다."

    ps -eo pid,ppid,comm,args |
        grep -Ei '[l]ldb|[g]dbserver' ||
        echo "관련 Linux 프로세스가 없습니다."
fi