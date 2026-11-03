using System.Globalization;
using UnrealSharp.Attributes;
using UnrealSharp.Engine;
using UnrealSharp.UMG;
using UnrealSharp.UnrealSoulslikeCSharp;

namespace ManagedUnrealSoulslikeCSharp;

// Mirrors ASoulslikePlayerController: game-only input mode, plus unattended-run switches for
// verifying and comparing builds:
//   -AutoShot=<seconds>   take a screenshot every N seconds (Saved/Screenshots)
//   -QuitAfter=<seconds>  exit the game after N seconds
//   -Benchmark=<seconds>  after a 5 s warm-up, record frame/game-thread times and memory for N
//                         seconds, write Saved/Benchmark/benchmark-csharp.json, then exit
// GGameThreadTime, peak memory, FScreenshotRequest and FMath::RandInit aren't reflected; they go
// through SoulslikeNative (the stub module) so the measurements are the same calls as in C++.
[UClass]
public partial class ASoulslikePlayerController : APlayerController
{
    private const float BenchmarkWarmup = 5.0f;

    private FTimerHandle _autoShotHandle;
    private FTimerHandle _quitHandle;
    private int _shotIndex;

    private float _benchmarkSeconds;
    private float _benchmarkElapsed;
    private List<float> _frameMs = [];
    private List<float> _gameThreadMs = [];

    public override void BeginPlay()
    {
        base.BeginPlay();
        if (!IsLocalController())
        {
            return;
        }

        UWidgetBlueprintLibrary.SetInputModeGameOnly(this, true);
        ShowMouseCursor = false;

        if (CommandLineSwitches.TryValue("AutoShot=", out float interval) && interval > 0.0f)
        {
            _autoShotHandle = SystemLibrary.SetTimer(TakeAutoShot, interval, true);
        }
        if (CommandLineSwitches.TryValue("QuitAfter=", out float quitAfter) && quitAfter > 0.0f)
        {
            _quitHandle = SystemLibrary.SetTimer(QuitGame, quitAfter, false);
        }
        if (CommandLineSwitches.TryValue("Benchmark=", out float benchmarkSeconds) && benchmarkSeconds > 0.0f)
        {
            _benchmarkSeconds = benchmarkSeconds;
            SoulslikeNative.SeedRandom(1745);
            int capacity = (int)MathF.Ceiling(benchmarkSeconds * 240.0f);
            _frameMs = new List<float>(capacity);
            _gameThreadMs = new List<float>(capacity);
        }
    }

    public override void Tick(float deltaSeconds)
    {
        base.Tick(deltaSeconds);
        if (_benchmarkSeconds <= 0.0f || !IsLocalController())
        {
            return;
        }

        _benchmarkElapsed += deltaSeconds;
        if (_benchmarkElapsed < BenchmarkWarmup)
        {
            return;
        }
        _frameMs.Add(deltaSeconds * 1000.0f);
        _gameThreadMs.Add(SoulslikeNative.ReadGameThreadMs());
        if (_benchmarkElapsed >= BenchmarkWarmup + _benchmarkSeconds)
        {
            WriteBenchmark();
            _benchmarkSeconds = 0.0f;
            QuitGame();
        }
    }

    private static float Percentile(List<float> values, float p)
    {
        if (values.Count == 0)
        {
            return 0.0f;
        }
        var sorted = new List<float>(values);
        sorted.Sort();
        int index = Math.Clamp((int)MathF.Floor(p * (sorted.Count - 1)), 0, sorted.Count - 1);
        return sorted[index];
    }

    private static float Mean(List<float> values)
    {
        double sum = 0.0;
        foreach (float value in values)
        {
            sum += value;
        }
        return values.Count == 0 ? 0.0f : (float)(sum / values.Count);
    }

    private void WriteBenchmark()
    {
        float averageMs = Mean(_frameMs);
        float p99Ms = Percentile(_frameMs, 0.99f);
        CultureInfo c = CultureInfo.InvariantCulture;

        string json =
            "{\n" +
            "  \"language\": \"csharp\",\n" +
            $"  \"seconds\": {(_benchmarkElapsed - BenchmarkWarmup).ToString("F1", c)},\n" +
            $"  \"frames\": {_frameMs.Count.ToString(c)},\n" +
            $"  \"avg_frame_ms\": {averageMs.ToString("F3", c)},\n" +
            $"  \"p50_frame_ms\": {Percentile(_frameMs, 0.5f).ToString("F3", c)},\n" +
            $"  \"p99_frame_ms\": {p99Ms.ToString("F3", c)},\n" +
            $"  \"avg_fps\": {(averageMs > 0.0f ? 1000.0f / averageMs : 0.0f).ToString("F1", c)},\n" +
            $"  \"one_percent_low_fps\": {(p99Ms > 0.0f ? 1000.0f / p99Ms : 0.0f).ToString("F1", c)},\n" +
            $"  \"avg_game_thread_ms\": {Mean(_gameThreadMs).ToString("F3", c)},\n" +
            $"  \"p99_game_thread_ms\": {Percentile(_gameThreadMs, 0.99f).ToString("F3", c)},\n" +
            $"  \"peak_used_physical_mb\": {SoulslikeNative.ReadPeakUsedPhysicalMB().ToString("F1", c)}\n" +
            "}\n";

        string path = Path.Combine(SoulslikeNative.ResolveSavedDir(), "Benchmark", "benchmark-csharp.json");
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllText(path, json);
        LogSoulslike.Log($"BENCHMARK_WRITTEN {path}");
    }

    [UFunction]
    private void TakeAutoShot()
    {
        SoulslikeNative.RequestScreenshot($"AutoShot_{_shotIndex++:000}");
    }

    [UFunction]
    private void QuitGame()
    {
        SystemLibrary.ExecuteConsoleCommand("quit", this);
    }
}

// FParse::Value / FParse::Param over the process command line.
public static class CommandLineSwitches
{
    public static bool TryValue(string key, out float value)
    {
        value = 0.0f;
        foreach (string raw in Environment.GetCommandLineArgs())
        {
            string arg = raw.TrimStart('-');
            if (arg.StartsWith(key, StringComparison.OrdinalIgnoreCase)
                && float.TryParse(arg.AsSpan(key.Length), NumberStyles.Float, CultureInfo.InvariantCulture, out value))
            {
                return true;
            }
        }
        return false;
    }

    public static bool HasParam(string name)
    {
        foreach (string raw in Environment.GetCommandLineArgs())
        {
            if (raw.StartsWith('-') && raw.AsSpan(1).Equals(name, StringComparison.OrdinalIgnoreCase))
            {
                return true;
            }
        }
        return false;
    }
}
