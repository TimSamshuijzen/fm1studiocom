<#
check_usb_audio.ps1 - records an FM-1 over USB and checks the recording: the test of USB audio.

    powershell -ExecutionPolicy Bypass -File testprograms\usb-audio-test\check_usb_audio.ps1

Install the package of testprograms/usb-audio-test on the FM-1 first, and connect the FM-1 to this computer
with a USB cable that carries data. The script:

  1. finds the FM-1 on USB (fm1-base's USB identity, VID 1209 PID 0001) and tells if its firmware has a USB
     audio input (bcdDevice, the REV of its hardware ID);
  2. finds the recording device of the FM-1 (WinMM, by the USB path of the device);
  3. records it (10 seconds; -Seconds), as 16-bit stereo at 44100 Hz, and saves the recording
     (usb-audio-test.wav in the current folder; -Out);
  4. checks the recording: a 689 Hz sine on the left and a 441 Hz sine on the right (periods of 64 and 100
     frames), their level (-23 dBFS RMS, whatever MASTER is), and that no frame was lost or repeated (each frame equals the frame one
     period before it).

It sends nothing to the FM-1. -WavFile FILE checks a recording that was made before (steps 1 to 3 do not run).
The exit code is 0 when all checks pass, else 1.
#>
param(
    [double]$Seconds = 10,
    [string]$Out = 'usb-audio-test.wav',
    [string]$WavFile = ''
)
$ErrorActionPreference = 'Stop'

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

public static class Fm1UsbAudio
{
    [StructLayout(LayoutKind.Sequential)]
    struct WAVEFORMATEX { public ushort wFormatTag, nChannels; public uint nSamplesPerSec, nAvgBytesPerSec; public ushort nBlockAlign, wBitsPerSample, cbSize; }

    [StructLayout(LayoutKind.Sequential)]
    struct WAVEHDR { public IntPtr lpData; public uint dwBufferLength, dwBytesRecorded; public IntPtr dwUser; public uint dwFlags, dwLoops; public IntPtr lpNext, reserved; }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct WAVEINCAPS { public ushort wMid, wPid; public uint vDriverVersion; [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string szPname; public uint dwFormats; public ushort wChannels, wReserved1; }

    [DllImport("winmm.dll")] static extern uint waveInGetNumDevs();
    [DllImport("winmm.dll", CharSet = CharSet.Unicode)] static extern uint waveInGetDevCapsW(UIntPtr id, ref WAVEINCAPS caps, uint size);
    [DllImport("winmm.dll")] static extern uint waveInMessage(IntPtr hwi, uint msg, IntPtr dw1, IntPtr dw2);
    [DllImport("winmm.dll")] static extern uint waveInOpen(out IntPtr hwi, uint id, ref WAVEFORMATEX format, IntPtr callback, IntPtr instance, uint flags);
    [DllImport("winmm.dll")] static extern uint waveInPrepareHeader(IntPtr hwi, IntPtr hdr, uint size);
    [DllImport("winmm.dll")] static extern uint waveInUnprepareHeader(IntPtr hwi, IntPtr hdr, uint size);
    [DllImport("winmm.dll")] static extern uint waveInAddBuffer(IntPtr hwi, IntPtr hdr, uint size);
    [DllImport("winmm.dll")] static extern uint waveInStart(IntPtr hwi);
    [DllImport("winmm.dll")] static extern uint waveInReset(IntPtr hwi);
    [DllImport("winmm.dll")] static extern uint waveInClose(IntPtr hwi);

    const uint DRV_QUERYDEVICEINTERFACE = 0x80C, DRV_QUERYDEVICEINTERFACESIZE = 0x80D;
    const uint WHDR_DONE = 1;
    public const int Rate = 44100;

    // The recording devices: their WinMM number, name (at most 31 characters) and device interface path.
    public static List<string[]> Devices()
    {
        var list = new List<string[]>();
        uint count = waveInGetNumDevs();
        for (uint id = 0; id < count; id++) {
            var caps = new WAVEINCAPS();
            if (waveInGetDevCapsW((UIntPtr)id, ref caps, (uint)Marshal.SizeOf(typeof(WAVEINCAPS))) != 0) continue;
            string path = "";
            IntPtr size = Marshal.AllocHGlobal(4);
            try {
                Marshal.WriteInt32(size, 0);
                if (waveInMessage((IntPtr)id, DRV_QUERYDEVICEINTERFACESIZE, size, IntPtr.Zero) == 0) {
                    int bytes = Marshal.ReadInt32(size);
                    if (bytes > 0) {
                        IntPtr text = Marshal.AllocHGlobal(bytes);
                        try {
                            if (waveInMessage((IntPtr)id, DRV_QUERYDEVICEINTERFACE, text, (IntPtr)bytes) == 0)
                                path = Marshal.PtrToStringUni(text);
                        } finally { Marshal.FreeHGlobal(text); }
                    }
                }
            } finally { Marshal.FreeHGlobal(size); }
            list.Add(new string[] { id.ToString(), caps.szPname, path ?? "" });
        }
        return list;
    }

    // Records `seconds` from device `id` as 16-bit stereo at 44100 Hz. Gives the samples (L R L R ...) and, in
    // `framesPerSecond`, the frames that came in a second of this computer's clock.
    public static short[] Record(uint id, double seconds, out double framesPerSecond)
    {
        var format = new WAVEFORMATEX { wFormatTag = 1, nChannels = 2, nSamplesPerSec = Rate, nAvgBytesPerSec = Rate * 4, nBlockAlign = 4, wBitsPerSample = 16 };
        IntPtr wave;
        uint error = waveInOpen(out wave, id, ref format, IntPtr.Zero, IntPtr.Zero, 0);
        if (error != 0) throw new Exception("waveInOpen gave error " + error + ": the FM-1's recording device did not open");
        const int buffers = 16, frames = 2205;   // 16 buffers of 50 ms
        int headerSize = Marshal.SizeOf(typeof(WAVEHDR)), flagsAt = (int)Marshal.OffsetOf(typeof(WAVEHDR), "dwFlags");
        int recordedAt = (int)Marshal.OffsetOf(typeof(WAVEHDR), "dwBytesRecorded");
        var headers = new IntPtr[buffers];
        var data = new MemoryStream();
        long wanted = (long)(seconds * Rate) * 4;
        var clock = new Stopwatch();
        long firstBytes = -1, firstTicks = 0;
        try {
            for (int i = 0; i < buffers; i++) {
                var header = new WAVEHDR { lpData = Marshal.AllocHGlobal(frames * 4), dwBufferLength = (uint)(frames * 4) };
                headers[i] = Marshal.AllocHGlobal(headerSize);
                Marshal.StructureToPtr(header, headers[i], false);
                waveInPrepareHeader(wave, headers[i], (uint)headerSize);
                waveInAddBuffer(wave, headers[i], (uint)headerSize);
            }
            waveInStart(wave);
            clock.Start();
            int next = 0;
            while (data.Length < wanted) {
                IntPtr h = headers[next];
                if ((Marshal.ReadInt32(h, flagsAt) & WHDR_DONE) == 0) {
                    if (clock.Elapsed.TotalSeconds > seconds + 5) throw new Exception("the recording device gave no sound for 5 s");
                    Thread.Sleep(5);
                    continue;
                }
                var header = (WAVEHDR)Marshal.PtrToStructure(h, typeof(WAVEHDR));
                int bytes = Marshal.ReadInt32(h, recordedAt);
                var chunk = new byte[bytes];
                Marshal.Copy(header.lpData, chunk, 0, bytes);
                if (firstBytes < 0) { firstBytes = data.Length + bytes; firstTicks = clock.ElapsedTicks; }
                data.Write(chunk, 0, bytes);
                Marshal.WriteInt32(h, flagsAt, Marshal.ReadInt32(h, flagsAt) & ~(int)WHDR_DONE);
                waveInAddBuffer(wave, h, (uint)headerSize);
                next = (next + 1) % buffers;
            }
            double span = (double)(clock.ElapsedTicks - firstTicks) / Stopwatch.Frequency;
            framesPerSecond = span > 0 ? (data.Length - firstBytes) / 4.0 / span : 0;
        } finally {
            waveInReset(wave);
            foreach (IntPtr h in headers) {
                if (h == IntPtr.Zero) continue;
                waveInUnprepareHeader(wave, h, (uint)headerSize);
                Marshal.FreeHGlobal(((WAVEHDR)Marshal.PtrToStructure(h, typeof(WAVEHDR))).lpData);
                Marshal.FreeHGlobal(h);
            }
            waveInClose(wave);
        }
        byte[] all = data.ToArray();
        var samples = new short[all.Length / 2];
        Buffer.BlockCopy(all, 0, samples, 0, samples.Length * 2);
        return samples;
    }

    public static void WriteWav(string path, short[] samples)
    {
        using (var w = new BinaryWriter(File.Create(path))) {
            w.Write(Encoding.ASCII.GetBytes("RIFF")); w.Write(36 + samples.Length * 2); w.Write(Encoding.ASCII.GetBytes("WAVEfmt "));
            w.Write(16); w.Write((short)1); w.Write((short)2); w.Write(Rate); w.Write(Rate * 4); w.Write((short)4); w.Write((short)16);
            w.Write(Encoding.ASCII.GetBytes("data")); w.Write(samples.Length * 2);
            foreach (short s in samples) w.Write(s);
        }
    }

    // A WAV file of 16-bit stereo PCM: its samples (L R L R ...).
    public static short[] ReadWav(string path)
    {
        byte[] b = File.ReadAllBytes(path);
        if (b.Length < 12 || Encoding.ASCII.GetString(b, 0, 4) != "RIFF" || Encoding.ASCII.GetString(b, 8, 4) != "WAVE")
            throw new Exception(path + " is not a WAV file");
        bool pcm = false;
        for (int at = 12; at + 8 <= b.Length;) {
            string id = Encoding.ASCII.GetString(b, at, 4);
            int size = BitConverter.ToInt32(b, at + 4);
            if (id == "fmt ") {
                int tag = BitConverter.ToUInt16(b, at + 8), channels = BitConverter.ToUInt16(b, at + 10), bits = BitConverter.ToUInt16(b, at + 22);
                if (tag == 0xFFFE && size >= 40) tag = BitConverter.ToUInt16(b, at + 32);   // WAVE_FORMAT_EXTENSIBLE: its subformat
                pcm = tag == 1 && channels == 2 && bits == 16;
                if (!pcm) throw new Exception(path + " is not 16-bit stereo PCM");
            } else if (id == "data") {
                if (!pcm) throw new Exception(path + " has no fmt chunk before its data");
                size = Math.Min(size, b.Length - at - 8);
                var samples = new short[size / 4 * 2];
                Buffer.BlockCopy(b, at + 8, samples, 0, samples.Length * 2);
                return samples;
            }
            at += 8 + size + (size & 1);
        }
        throw new Exception(path + " has no data chunk");
    }

    // The power of the frequency `cycles` (in cycles a frame) in x[from .. from + n), as a part of all the power there.
    static double Part(short[] s, int channel, int from, int n, double cycles)
    {
        double w = 2 * Math.PI * cycles, c = 2 * Math.Cos(w), s1 = 0, s2 = 0, total = 0;
        for (int i = 0; i < n; i++) {
            double x = s[2 * (from + i) + channel];
            double s0 = x + c * s1 - s2;
            s2 = s1; s1 = s0;
            total += x * x;
        }
        double power = s1 * s1 + s2 * s2 - c * s1 * s2;
        return total > 0 ? power * 2 / n / total : 0;
    }

    // The checks of one channel of the recording, from frame `from` on. Gives
    // { rms dBFS, peak, part of the power at its own sine, part at the other's, frames that differ a little from the
    //   frame one period before (away from lost or repeated frames), lost or repeated frames (events), the first frame of
    //   the first such event (-1: none) }.
    public static double[] Check(short[] s, int channel, int from, int period, int otherPeriod)
    {
        int frames = s.Length / 2;
        double sum = 0; int peak = 0;
        for (int i = from; i < frames; i++) { int x = s[2 * i + channel]; sum += (double)x * x; peak = Math.Max(peak, Math.Abs(x)); }
        int n = frames - from;
        double rms = n > 0 ? Math.Sqrt(sum / n) : 0;
        double db = rms > 0 ? 20 * Math.Log10(rms / 32768) : -200;
        // Goertzel over whole periods of both sines (64 x 100 frames: 1600 is a multiple of both periods), in windows of
        // 6400 frames, the worst window counts. The FM-1's clock is 0.04 % faster than 44100 Hz, far inside a bin.
        int window = 6400;
        double own = 1, other = 0;
        for (int at = from; at + window <= frames; at += window) {
            own = Math.Min(own, Part(s, channel, at, window, 1.0 / period));
            other = Math.Max(other, Part(s, channel, at, window, 1.0 / otherPeriod));
        }
        // A lost or repeated frame: from there on, for one period, frames differ from the frame one period before.
        // A small difference everywhere (a level that Windows changes, rounding) is no lost frame: the tolerance.
        int tolerance = Math.Max(3, peak / 100), differ = 0, events = 0, first = -1, lastBad = -1000000;
        var near = new bool[frames];   // within a period of a lost or repeated frame
        for (int i = from + period; i < frames; i++) {
            int d = Math.Abs(s[2 * i + channel] - s[2 * (i - period) + channel]);
            if (d > tolerance) {
                if (i - lastBad > period) { events++; if (first < 0) first = i; }
                lastBad = i;
                for (int j = Math.Max(0, i - period); j < Math.Min(frames, i + period + 1); j++) near[j] = true;
            }
        }
        for (int i = from + period; i < frames; i++)
            if (!near[i] && s[2 * i + channel] != s[2 * (i - period) + channel]) differ++;
        return new double[] { db, peak, own, other, differ, events, first };
    }
}
'@

$failed = 0
function Say-Ok([string]$text) { Write-Host "  ok    $text" }
function Say-Note([string]$text) { Write-Host "  note  $text" -ForegroundColor Yellow }
function Say-Fail([string]$text) { Write-Host "  FAIL  $text" -ForegroundColor Red; $script:failed++ }

if ($WavFile) {
    Write-Host "check the recording $WavFile"
    $samples = [Fm1UsbAudio]::ReadWav((Resolve-Path $WavFile).Path)
    $rate = 0
} else {
    # 1. The FM-1 on USB: fm1-base's identity. bcdDevice (REV_xxxx) codes the USB functions of the firmware (BCD_LO in
    # felucca-bsp's chip/usb.c): an FM-1 Studio package has REV_0390 with its USB audio input, REV_0300 without it.
    # Other firmwares code more parts in it, so the test is the recording device that Windows has (step 2).
    Write-Host 'the FM-1 on USB'
    $fm1 = @(Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -match '^USB\\VID_1209&PID_0001\\' })
    if (-not $fm1) {
        Say-Fail 'no FM-1 with an fm1-base firmware on USB (VID 1209, PID 0001). Connect it with a cable that carries data.'
        exit 1
    }
    $id = $fm1[0].InstanceId
    $name = (Get-PnpDeviceProperty -InstanceId $id -KeyName DEVPKEY_Device_BusReportedDeviceDesc).Data
    $hardware = (Get-PnpDeviceProperty -InstanceId $id -KeyName DEVPKEY_Device_HardwareIds).Data -join ' '
    $rev = if ($hardware -match 'REV_([0-9A-Fa-f]{4})') { [Convert]::ToInt32($Matches[1], 16) } else { -1 }
    $revText = if ($rev -ge 0) { 'REV_{0:X4}' -f $rev } else { 'no REV' }
    Say-Ok "`"$name`" ($revText)"

    # 2. Its recording device: WinMM gives the device interface path of each one, with the USB identity in it.
    $devices = [Fm1UsbAudio]::Devices()
    $device = $devices | Where-Object { $_[2] -match 'VID_1209&PID_0001' } | Select-Object -First 1
    if (-not $device) {
        Say-Fail 'Windows has no recording device of the FM-1: its firmware has no USB audio input (an FM-1 Studio package without it: REV_0300). Its recording devices:'
        foreach ($d in $devices) { Write-Host "          $($d[1])  $($d[2])" }
        exit 1
    }
    Say-Ok "recording device $($device[0]): `"$($device[1])`""

    # 3. The recording.
    Write-Host "record $Seconds s"
    $rate = 0.0
    $samples = [Fm1UsbAudio]::Record([uint32]$device[0], $Seconds, [ref]$rate)
    $outPath = [IO.Path]::GetFullPath((Join-Path (Get-Location) $Out))
    [Fm1UsbAudio]::WriteWav($outPath, $samples)
    Say-Ok ("{0:N0} frames, saved as {1}" -f ($samples.Length / 2), $outPath)
}

# 4. The checks. The first quarter second is left out: a recording can start with the device's own start.
Write-Host 'check the sound'
$from = 11025
$frames = $samples.Length / 2
if ($frames -lt $from + 6400) { Say-Fail "the recording is too short: $frames frames"; exit 1 }
$seconds = ($frames - $from) / 44100
$sides = @(
    @{ Name = 'LEFT'; Channel = 0; Period = 64; Other = 100; Tone = '689 Hz' },
    @{ Name = 'RIGHT'; Channel = 1; Period = 100; Other = 64; Tone = '441 Hz' }
)
$exact = $true
$checked = 0   # the sides with their sine: the others have no frame check
foreach ($side in $sides) {
    $r = [Fm1UsbAudio]::Check($samples, $side.Channel, $from, $side.Period, $side.Other)
    $db, $peak, $own, $other, $differ, $events, $first = $r
    if ($peak -lt 10) {
        Say-Fail "$($side.Name): silence. Is the sound on (PLAY on the FM-1)? Windows Settings > Privacy & security > Microphone: may desktop apps use the microphone?"
        continue
    }
    if ($other -ge 0.5) {
        Say-Fail ("{0}: the sine of the other side: left and right are swapped" -f $side.Name)
        continue
    }
    if ($own -lt 0.95 -or $other -gt 0.01) {
        Say-Fail ("{0}: not the {1} sine of usb-audio-test ({2:P1} of the sound). Is that program on the FM-1?" -f $side.Name, $side.Tone, $own)
        continue
    }
    $checked++
    Say-Ok ("{0}: the {1} sine ({2:P1} of the sound), {3:N1} dBFS RMS, peak {4}" -f $side.Name, $side.Tone, $own, $db, $peak)
    if ([Math]::Abs($db + 23.0) -gt 1.0) {
        Say-Note ("{0}: the level is {1:N1} dBFS RMS, not -23.0 as the program plays it. Is the recording level of the FM-1 100 % (Windows sound settings)?" -f $side.Name, $db)
    }
    if ($events -eq 1) {
        Say-Fail ("{0}: a frame was lost or repeated at {1:N3} s" -f $side.Name, ($first / 44100))
    } elseif ($events -gt 1) {
        Say-Fail ("{0}: frames were lost or repeated {1} times, the first at {2:N3} s" -f $side.Name, $events, ($first / 44100))
    } else {
        Say-Ok ("{0}: no frame lost or repeated in {1:N2} s" -f $side.Name, $seconds)
    }
    if ($differ -ne 0) { $exact = $false }
}
if ($checked -and $exact) {
    Say-Ok 'bit for bit: each frame is the same as the frame one period before it'
} elseif ($checked) {
    Say-Note 'not bit for bit: the samples differ a little from period to period (Windows changes the level, or an effect is on)'
}
if ($rate -gt 0) {
    Say-Note ("the FM-1 sent {0:N0} frames a second by this computer's clock (its own clock: about 44,118)" -f $rate)
}
if ($failed) { Write-Host "USB audio: $failed checks failed" -ForegroundColor Red; exit 1 }
Write-Host 'USB audio: all checks pass' -ForegroundColor Green
exit 0
