// Copyright Epic Games, Inc. All Rights Reserved.

import { registerHordePlugin, MountType } from "..";
import { ExamplePluginView } from "./examplePluginView";

registerHordePlugin({
   id: "exampleplugin",
   routes: [{ path: "exampleplugin", element: <ExamplePluginView /> }],
   mount: {
      type: MountType.TopNav,
      context: "Tools",
      text: "ExamplePlugin",
      route: `/exampleplugin`
   }
})