// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <carbon/Common.h>
#include <carbon/io/JsonIO.h>
#include <algorithm>
#include <cmath>
#include <string>

CARBON_NAMESPACE_BEGIN(TITAN_NAMESPACE)

enum class ScheduleCurve { Static, Linear, Quadratic, Log };

struct WeightSchedule {
    float start = 1.0f;
    float end = 1.0f;
    ScheduleCurve curve = ScheduleCurve::Static;

    float Evaluate(int iter, int totalIters) const
    {
        if (curve == ScheduleCurve::Static || totalIters <= 1) return start;
        const float t = std::clamp(float(iter) / float(totalIters - 1), 0.0f, 1.0f);
        float c = t;
        if (curve == ScheduleCurve::Quadratic) c = t * t;
        else if (curve == ScheduleCurve::Log)  c = std::log(1.0f + t * 9.0f) / std::log(10.0f);
        return start + (end - start) * c;
    }

    JsonElement ToJson() const
    {
        JsonElement j(JsonElement::JsonType::Object);
        j.Insert("start", JsonElement(static_cast<double>(start)));
        if (curve != ScheduleCurve::Static)
        {
            j.Insert("end", JsonElement(static_cast<double>(end)));
            j.Insert("curve", JsonElement(CurveToString(curve)));
        }
        return j;
    }

    void FromJson(const JsonElement& j)
    {
        if (!j.IsObject()) return;
        if (j.Contains("start"))     start     = j["start"].Value<float>();
        if (j.Contains("end"))       end       = j["end"].Value<float>();
        if (j.Contains("curve"))
            curve = CurveFromString(j["curve"].String());
        else
            curve = ScheduleCurve::Static;
        if (curve == ScheduleCurve::Static) end = start;
    }

    static const char* CurveToString(ScheduleCurve c)
    {
        switch (c) {
            case ScheduleCurve::Linear:    return "linear";
            case ScheduleCurve::Quadratic: return "quadratic";
            case ScheduleCurve::Log:       return "log";
            default:                       return "static";
        }
    }

    static ScheduleCurve CurveFromString(const std::string& s)
    {
        if (s == "linear")    return ScheduleCurve::Linear;
        if (s == "quadratic") return ScheduleCurve::Quadratic;
        if (s == "log")       return ScheduleCurve::Log;
        return ScheduleCurve::Static;
    }
};

CARBON_NAMESPACE_END(TITAN_NAMESPACE)
