// Copyright Epic Games, Inc. All Rights Reserved.

import { MountType, registerHordePlugin } from "hordePlugins";
import { PerformanceTrendsView } from "./PerformanceTrendView";

registerHordePlugin({
    id: "performancetrends",
    routes: [
        { path: "performancetrends", element: <PerformanceTrendsView/> },
    ],
    mounts: [
        {
            type: MountType.TopNav,
            context: "Tools",
            text: "Analytics - Performance Trends",
            route: "/performancetrends"
        }
    ]
});