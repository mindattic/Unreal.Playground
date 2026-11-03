using System.Text.Json;
using UnrealSharp.CoreUObject;
using UnrealSharp.UnrealSoulslikeCSharp;

namespace ManagedUnrealSoulslikeCSharp;

// Mirrors FHebridesTerrain (read-only parts) from SoulslikeData.cpp: the island's heightmap
// (Content/Terrain/HebridesHeightmap.r16, raw little-endian uint16) and its landmarks
// (HebridesLandmarks.json). Same constants and the same sampling maths as the C++ build.
public sealed class HebridesTerrain
{
    public const float CellSize = 300.0f;
    public const float HeightScale = 16000.0f;

    private static readonly Lazy<HebridesTerrain> Instance = new(() =>
    {
        var terrain = new HebridesTerrain();
        terrain.Load();
        return terrain;
    });

    public static HebridesTerrain Get() => Instance.Value;

    public int Resolution { get; private set; }
    public float SeaLevel { get; private set; } = 0.2f;
    public float[] Heights { get; private set; } = [];

    public FVector2D LandingBeach { get; private set; } = new(0.5, 0.85);
    public FVector2D Broch { get; private set; } = new(0.0, 0.0);
    public FVector2D StandingStones { get; private set; } = new(0.0, 0.0);
    public FVector2D Keep { get; private set; } = new(0.5, 0.5);
    public FVector2D Boss { get; private set; } = new(0.5, 0.5);
    public List<FVector2D> PlayerStarts { get; } = [];
    public List<FVector2D> Crofts { get; } = [];
    public List<FVector2D> SkeletonSpawns { get; } = [];

    public bool IsValid() => Resolution > 1 && Heights.Length == Resolution * Resolution;

    public float WorldSize() => (Resolution - 1) * CellSize;

    public float GridZ(int x, int y)
    {
        if (!IsValid())
        {
            return 0.0f;
        }
        x = Math.Clamp(x, 0, Resolution - 1);
        y = Math.Clamp(y, 0, Resolution - 1);
        return (Heights[y * Resolution + x] - SeaLevel) * HeightScale;
    }

    public float SampleZ(float u, float v)
    {
        if (!IsValid())
        {
            return 0.0f;
        }
        float fx = Math.Clamp(u, 0.0f, 1.0f) * (Resolution - 1);
        float fy = Math.Clamp(v, 0.0f, 1.0f) * (Resolution - 1);
        int x0 = (int)MathF.Floor(fx);
        int y0 = (int)MathF.Floor(fy);
        float tx = fx - x0;
        float ty = fy - y0;
        float top = Lerp(GridZ(x0, y0), GridZ(x0 + 1, y0), tx);
        float bottom = Lerp(GridZ(x0, y0 + 1), GridZ(x0 + 1, y0 + 1), tx);
        return Lerp(top, bottom, ty);
    }

    public FVector ToWorld(FVector2D uv)
    {
        float size = WorldSize();
        return new FVector((uv.X - 0.5) * size, (uv.Y - 0.5) * size, SampleZ((float)uv.X, (float)uv.Y));
    }

    public FVector2D ToUV(FVector world)
    {
        float size = Math.Max(WorldSize(), 1.0f);
        return new FVector2D(world.X / size + 0.5, world.Y / size + 0.5);
    }

    private static float Lerp(float a, float b, float t) => a + (b - a) * t;

    private void Load()
    {
        string contentDir = SoulslikeNative.ResolveContentDir();
        string heightmapPath = Path.Combine(contentDir, "Terrain", "HebridesHeightmap.r16");
        if (!File.Exists(heightmapPath))
        {
            LogSoulslike.LogError($"Missing heightmap {heightmapPath}");
            return;
        }

        byte[] raw = File.ReadAllBytes(heightmapPath);
        int sampleCount = raw.Length / 2;
        int width = (int)MathF.Round(MathF.Sqrt(sampleCount));
        if (width < 2 || width * width * 2 != raw.Length)
        {
            LogSoulslike.LogError($"Heightmap {heightmapPath} must be square raw uint16 ({raw.Length} bytes)");
            return;
        }

        var heights = new float[sampleCount];
        for (int i = 0; i < sampleCount; i++)
        {
            ushort sample = (ushort)(raw[i * 2] | (raw[i * 2 + 1] << 8));
            heights[i] = sample / 65535.0f;
        }
        Heights = heights;
        Resolution = width;

        string landmarksPath = Path.Combine(contentDir, "Terrain", "HebridesLandmarks.json");
        if (SoulslikeJson.TryLoad(landmarksPath, out JsonDocument? document))
        {
            using (document)
            {
                JsonElement root = document!.RootElement;
                if (root.TryGetProperty("sea_level", out JsonElement seaLevel))
                {
                    SeaLevel = seaLevel.GetSingle();
                }
                LandingBeach = SoulslikeJson.ReadPoint(root, "landing_beach", LandingBeach);
                Broch = SoulslikeJson.ReadPoint(root, "broch", Broch);
                StandingStones = SoulslikeJson.ReadPoint(root, "standing_stones", StandingStones);
                Keep = SoulslikeJson.ReadPoint(root, "keep", Keep);
                Boss = SoulslikeJson.ReadPoint(root, "boss", Boss);
                SoulslikeJson.ReadPoints(root, "player_starts", PlayerStarts);
                SoulslikeJson.ReadPoints(root, "crofts", Crofts);
                SoulslikeJson.ReadPoints(root, "skeleton_spawns", SkeletonSpawns);
            }
        }
        if (PlayerStarts.Count == 0)
        {
            PlayerStarts.Add(LandingBeach);
        }
        LogSoulslike.Log($"Island heightmap {Resolution}x{Resolution} loaded, {SkeletonSpawns.Count} skeleton spawns");
    }
}

public sealed record SoulslikeAttachment(string Socket, string Mesh);

public sealed class SoulslikeVisual
{
    public string Name { get; init; } = "";
    public string Mesh { get; init; } = "";
    public List<SoulslikeAttachment> Attachments { get; } = [];
    public bool IsSet() => Mesh.Length > 0;
}

// Mirrors FSoulslikeAssets: imported character art paths from Content/Characters/CharacterAssets.json.
public sealed class SoulslikeAssets
{
    private static readonly Lazy<SoulslikeAssets> Instance = new(() =>
    {
        var assets = new SoulslikeAssets();
        assets.Load();
        return assets;
    });

    public static SoulslikeAssets Get() => Instance.Value;

    public SoulslikeVisual Knight { get; private set; } = new();
    public SoulslikeVisual Boss { get; private set; } = new();
    public List<SoulslikeVisual> Skeletons { get; } = [];
    public Dictionary<string, string> Anims { get; } = [];

    private void Load()
    {
        string path = Path.Combine(SoulslikeNative.ResolveContentDir(), "Characters", "CharacterAssets.json");
        if (!SoulslikeJson.TryLoad(path, out JsonDocument? document))
        {
            return;
        }

        using (document)
        {
            JsonElement root = document!.RootElement;
            if (root.TryGetProperty("knight", out JsonElement knight))
            {
                Knight = ReadVisual(knight);
            }
            if (root.TryGetProperty("boss", out JsonElement boss))
            {
                Boss = ReadVisual(boss);
            }
            if (root.TryGetProperty("skeletons", out JsonElement skeletons) && skeletons.ValueKind == JsonValueKind.Array)
            {
                foreach (JsonElement item in skeletons.EnumerateArray())
                {
                    SoulslikeVisual visual = ReadVisual(item);
                    if (visual.IsSet())
                    {
                        Skeletons.Add(visual);
                    }
                }
            }
            if (root.TryGetProperty("anims", out JsonElement anims) && anims.ValueKind == JsonValueKind.Object)
            {
                foreach (JsonProperty pair in anims.EnumerateObject())
                {
                    Anims[pair.Name] = pair.Value.GetString() ?? "";
                }
            }
        }
        LogSoulslike.Log($"Character assets: knight={(Knight.IsSet() ? "yes" : "no")}, {Skeletons.Count} skeletons, {Anims.Count} anims");
    }

    private static SoulslikeVisual ReadVisual(JsonElement element)
    {
        var visual = new SoulslikeVisual
        {
            Name = element.TryGetProperty("name", out JsonElement name) ? name.GetString() ?? "" : "",
            Mesh = element.TryGetProperty("mesh", out JsonElement mesh) ? mesh.GetString() ?? "" : "",
        };
        if (element.TryGetProperty("attachments", out JsonElement attachments) && attachments.ValueKind == JsonValueKind.Array)
        {
            foreach (JsonElement item in attachments.EnumerateArray())
            {
                if (item.TryGetProperty("socket", out JsonElement socket) && item.TryGetProperty("mesh", out JsonElement propMesh))
                {
                    visual.Attachments.Add(new SoulslikeAttachment(socket.GetString() ?? "", propMesh.GetString() ?? ""));
                }
            }
        }
        return visual;
    }
}

internal static class SoulslikeJson
{
    public static bool TryLoad(string path, out JsonDocument? document)
    {
        document = null;
        if (!File.Exists(path))
        {
            LogSoulslike.LogWarning($"Missing {path}");
            return false;
        }
        try
        {
            document = JsonDocument.Parse(File.ReadAllText(path));
            return true;
        }
        catch (JsonException exception)
        {
            LogSoulslike.LogError($"Could not parse {path}: {exception.Message}");
            return false;
        }
    }

    public static FVector2D ReadPoint(JsonElement root, string field, FVector2D fallback)
    {
        if (root.TryGetProperty(field, out JsonElement pair) && pair.ValueKind == JsonValueKind.Array && pair.GetArrayLength() == 2)
        {
            return new FVector2D(pair[0].GetDouble(), pair[1].GetDouble());
        }
        return fallback;
    }

    public static void ReadPoints(JsonElement root, string field, List<FVector2D> into)
    {
        if (!root.TryGetProperty(field, out JsonElement list) || list.ValueKind != JsonValueKind.Array)
        {
            return;
        }
        foreach (JsonElement pair in list.EnumerateArray())
        {
            if (pair.ValueKind == JsonValueKind.Array && pair.GetArrayLength() == 2)
            {
                into.Add(new FVector2D(pair[0].GetDouble(), pair[1].GetDouble()));
            }
        }
    }
}
