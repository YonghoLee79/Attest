# 노트북(Windows PowerShell)에서 라즈베리파이 + 아두이노를 제어한다.
# 모든 명령은 ssh 로 라즈베리파이에서 실행된다.
#
#   .\pi.ps1 help            사용법
#   $env:PI_HOST = "todenny@100.111.61.65"   (기본값과 다르면 설정)
# param() 블록을 쓰면 "-i", "--json" 이 PowerShell 자체 옵션으로 해석되므로 $args 를 그대로 쓴다.
$Action = if ($args.Count -ge 1) { [string]$args[0] } else { "help" }
# 타입을 고정하지 않으면 인자가 1개일 때 배열이 문자열로 풀려 $Rest[0] 이 첫 글자가 된다.
[string[]]$Rest = @()
if ($args.Count -ge 2) { $Rest = $args[1..($args.Count - 1)] }

[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$PiHost = if ($env:PI_HOST) { $env:PI_HOST } else { "todenny@100.111.61.65" }
$Repo = "~/Attest"
$Branch = "claude/sweet-newton-fud3n6"
$script:rc = 0

# 원격 셸에 안전하게 넘기도록 작은따옴표로 감싼다.
function Quote([string]$s) { "'" + ($s -replace "'", "'\''") + "'" }

function Invoke-Pi([string]$cmd, [switch]$Tty) {
    $opts = @("-o", "ServerAliveInterval=30")
    if ($Tty) { $opts += "-t" }
    & ssh @opts $PiHost $cmd
    $script:rc = $LASTEXITCODE
}

function Send-Board([string]$board, [string[]]$cmds) {
    $q = ($cmds | ForEach-Object { Quote $_ }) -join " "
    Invoke-Pi "cd $Repo/pi && python3 -m hwtest.send $board $q"
}

function Need([int]$n, [string]$usage) {
    if ($Rest.Count -lt $n) { Write-Host "사용법: .\pi.ps1 $usage"; exit 2 }
}

$Heart = @("00", "66", "FF", "FF", "FF", "7E", "3C", "18")
$Smile = @("3C", "42", "A5", "81", "A5", "99", "42", "3C")

switch ($Action.ToLower()) {
    "setup-key" {
        # 비밀번호 없이 접속하도록 ssh 키를 라즈베리파이에 등록 (한 번만)
        $key = Join-Path $env:USERPROFILE ".ssh\id_ed25519"
        if (-not (Test-Path $key)) {
            Write-Host "ssh 키를 만듭니다. 질문이 나오면 Enter 만 누르세요."
            ssh-keygen -t ed25519 -f $key
        }
        Get-Content "$key.pub" | ssh $PiHost "mkdir -p ~/.ssh && chmod 700 ~/.ssh && cat >> ~/.ssh/authorized_keys && chmod 600 ~/.ssh/authorized_keys"
        $script:rc = $LASTEXITCODE
        if ($script:rc -eq 0) { Write-Host "완료. 이제 비밀번호 없이 접속됩니다." }
    }
    "update" {
        Write-Host "== 노트북"
        git -C $PSScriptRoot pull
        Write-Host "== 라즈베리파이"
        Invoke-Pi "if [ -d $Repo ]; then cd $Repo && git pull; else git clone -b $Branch https://github.com/YonghoLee79/Attest.git $Repo; fi"
    }
    "upload" { Invoke-Pi "cd $Repo && ./upload.sh $($Rest -join ' ')" -Tty }
    "test" { Invoke-Pi "cd $Repo/pi && python3 -m hwtest $($Rest -join ' ')" -Tty }
    "report" {
        scp "${PiHost}:Attest/pi/report.json" .
        $script:rc = $LASTEXITCODE
        if ($script:rc -eq 0) { Write-Host "report.json 을 현재 폴더에 받았습니다." }
    }
    "ports" { Invoke-Pi "ls -l /dev/ttyUSB* /dev/ttyACM* 2>/dev/null || echo '포트 없음'; command -v lsof >/dev/null && lsof /dev/ttyUSB* /dev/ttyACM* 2>/dev/null" }
    "diag" { Invoke-Pi "cd $Repo && ./diag.sh $($Rest -join ' ')" -Tty }
    "console" { Invoke-Pi "cd $Repo/pi && python3 -m hwtest.send" -Tty }
    "shell" { Invoke-Pi "cd $Repo && exec bash -l" -Tty }
    "send" {
        Need 2 "send ARD1|ARD2 `"명령`" [`"명령`" ...]"
        Send-Board $Rest[0] $Rest[1..($Rest.Count - 1)]
    }

    # ---- 아두이노 1번 ----
    "led" {
        Need 1 "led R G B | led off"
        if ($Rest[0] -eq "off") { Send-Board ARD1 @("LED CLEAR") }
        else { Need 3 "led R G B"; Send-Board ARD1 @("LED FILL $($Rest[0..2] -join ' ')") }
    }
    "pixel" { Need 4 "pixel 번호 R G B"; Send-Board ARD1 @("LED PIX $($Rest[0..3] -join ' ')") }
    "bright" { Need 1 "bright 0~255"; Send-Board ARD1 @("LED BRIGHT $($Rest[0])") }
    "step" {
        Need 1 "step 스텝수(+/-, 2048=1바퀴) [rpm]"
        $cmds = @()
        if ($Rest.Count -ge 2) { $cmds += "STEP SPEED $($Rest[1])" }
        $cmds += "STEP MOVE $($Rest[0])"
        $cmds += "STEP POS"
        Send-Board ARD1 $cmds
    }

    # ---- 아두이노 2번 ----
    "servo" { Need 1 "servo 0~180"; Send-Board ARD2 @("SERVO $($Rest[0])") }
    "rgb" {
        Need 1 "rgb R G B | rgb off"
        if ($Rest[0] -eq "off") { Send-Board ARD2 @("RGB 0 0 0") }
        else { Need 3 "rgb R G B"; Send-Board ARD2 @("RGB $($Rest[0..2] -join ' ')") }
    }
    "lcd" {
        Need 1 "lcd `"글자`" [줄 0|1] | lcd clear"
        if ($Rest[0] -eq "clear") { Send-Board ARD2 @("LCD CLEAR") }
        else {
            $row = if ($Rest.Count -ge 2) { $Rest[1] } else { "0" }
            Send-Board ARD2 @("LCD PRINT $row $($Rest[0])")
        }
    }
    "dot" {
        Need 1 "dot heart|smile|all|clear"
        switch ($Rest[0]) {
            "heart" { Send-Board ARD2 (0..7 | ForEach-Object { "DOT ROW $_ $($Heart[$_])" }) }
            "smile" { Send-Board ARD2 (0..7 | ForEach-Object { "DOT ROW $_ $($Smile[$_])" }) }
            "all" { Send-Board ARD2 @("DOT ALL") }
            default { Send-Board ARD2 @("DOT CLEAR") }
        }
    }
    "dist" { Send-Board ARD2 @("DIST") }
    "dht" { Send-Board ARD2 @("DHT") }
    "light" { Send-Board ARD2 @("LIGHT") }
    "sensors" { Send-Board ARD2 @("DIST", "DHT", "LIGHT") }

    default {
        @"
라즈베리파이($PiHost) 원격 제어

  준비
    .\pi.ps1 setup-key           ssh 키 등록 (비밀번호 입력 없이 쓰려면 한 번)
    .\pi.ps1 update              노트북 + 라즈베리파이 코드 최신으로
    .\pi.ps1 upload [포트1 포트2] 아두이노 펌웨어 업로드
    .\pi.ps1 ports               USB 포트 / 사용 중인 프로그램 확인
    .\pi.ps1 diag [포트]         업로드가 안 될 때 보드 종류/원인 진단 (보드에 쓰지 않음)

  테스트
    .\pi.ps1 test                통합 테스트 (자동 판정)
    .\pi.ps1 test -i             눈으로 확인하는 질문 포함
    .\pi.ps1 test --json report.json   후   .\pi.ps1 report   (결과 파일 받기)

  아두이노 1번
    .\pi.ps1 led 255 0 0         LED 스트립 전체 색      .\pi.ps1 led off
    .\pi.ps1 pixel 3 0 0 255     LED 한 개               .\pi.ps1 bright 128
    .\pi.ps1 step 512 [rpm]      스테핑 모터 (2048 = 1바퀴, 음수는 반대)

  아두이노 2번
    .\pi.ps1 servo 90            서보 각도
    .\pi.ps1 rgb 0 255 0         RGB LED                 .\pi.ps1 rgb off
    .\pi.ps1 lcd "Hello" 1       LCD 1번 줄              .\pi.ps1 lcd clear
    .\pi.ps1 dot heart           도트 (heart/smile/all/clear)
    .\pi.ps1 sensors             거리 + 온습도 + 조도    (dist / dht / light 개별도 가능)

  직접 명령
    .\pi.ps1 send ARD2 "SERVO 0" "SERVO 180"
    .\pi.ps1 console             대화형 (ARD1 LED FILL 0 0 255 처럼 입력, quit 종료)
    .\pi.ps1 shell               라즈베리파이 셸 접속
"@ | Write-Host
    }
}
exit $script:rc
