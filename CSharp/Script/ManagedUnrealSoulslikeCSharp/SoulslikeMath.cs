using UnrealSharp.CoreUObject;
using UnrealSharp.Engine;

namespace ManagedUnrealSoulslikeCSharp;

// The FMath / FVector helpers the C++ game uses, with the same formulas, so both builds compute
// identical numbers. Random draws go through KismetMathLibrary (FMath::FRand / FRandRange), the
// same global stream the C++ build uses, seeded identically by SoulslikeNative.SeedRandom.
public static class SoulslikeMath
{
    private const double SmallNumber = 1e-8;

    public static double Size2D(FVector v) => Math.Sqrt(v.X * v.X + v.Y * v.Y);

    public static double SizeSquared2D(FVector v) => v.X * v.X + v.Y * v.Y;

    public static double Size(FVector v) => Math.Sqrt(v.X * v.X + v.Y * v.Y + v.Z * v.Z);

    public static double Dot(FVector a, FVector b) => a.X * b.X + a.Y * b.Y + a.Z * b.Z;

    public static double Dist(FVector a, FVector b) => Size(Sub(a, b));

    public static double Dist2D(FVector a, FVector b) => Size2D(Sub(a, b));

    public static FVector Add(FVector a, FVector b) => new FVector(a.X + b.X, a.Y + b.Y, a.Z + b.Z);

    public static FVector Sub(FVector a, FVector b) => new FVector(a.X - b.X, a.Y - b.Y, a.Z - b.Z);

    public static FVector Scale(FVector v, double s) => new FVector(v.X * s, v.Y * s, v.Z * s);

    public static FVector GetSafeNormal2D(FVector v)
    {
        double squareSum = v.X * v.X + v.Y * v.Y;
        if (squareSum == 1.0)
        {
            return new FVector(v.X, v.Y, 0.0);
        }
        if (squareSum < SmallNumber)
        {
            return new FVector(0.0, 0.0, 0.0);
        }
        double scale = 1.0 / Math.Sqrt(squareSum);
        return new FVector(v.X * scale, v.Y * scale, 0.0);
    }

    public static bool IsNearlyZero(FVector v, double tolerance = 1e-4) =>
        Math.Abs(v.X) <= tolerance && Math.Abs(v.Y) <= tolerance && Math.Abs(v.Z) <= tolerance;

    // FVector::Rotation().Yaw
    public static double YawOf(FVector v) => Math.Atan2(v.Y, v.X) * (180.0 / Math.PI);

    public static double ClampAxis(double angle)
    {
        angle %= 360.0;
        return angle < 0.0 ? angle + 360.0 : angle;
    }

    // FMath::FixedTurn
    public static double FixedTurn(double current, double desired, double deltaRate)
    {
        if (deltaRate == 0.0)
        {
            return ClampAxis(current);
        }
        if (deltaRate >= 360.0)
        {
            return ClampAxis(desired);
        }

        double result = ClampAxis(current);
        current = result;
        desired = ClampAxis(desired);
        double rate = Math.Abs(deltaRate);

        if (current > desired)
        {
            result += current - desired < 180.0 ? -Math.Min(current - desired, rate) : Math.Min(desired + 360.0 - current, rate);
        }
        else
        {
            result += desired - current < 180.0 ? Math.Min(desired - current, rate) : -Math.Min(current + 360.0 - desired, rate);
        }
        return ClampAxis(result);
    }

    public static float Clamp(float value, float min, float max) => value < min ? min : (value > max ? max : value);

    public static float FRand() => (float)MathLibrary.RandomFloat();

    public static float FRandRange(float min, float max) => (float)MathLibrary.RandomFloatInRange(min, max);

    // FMath::RandPointInCircle: rejection-sample the unit square, same draws as C++.
    public static FVector2D RandPointInCircle(float radius)
    {
        double x, y;
        do
        {
            x = FRand() * 2.0 - 1.0;
            y = FRand() * 2.0 - 1.0;
        }
        while (x * x + y * y > 1.0);
        return new FVector2D(x * radius, y * radius);
    }
}
