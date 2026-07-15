<?xml version="1.0" encoding="utf-8"?>
<TpsData xmlns:xsd="http://www.w3.org/2001/XMLSchema" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">
  <Name>Vulkan SDK for Mac</Name>
  <!-- Software Name and Version  -->
<!-- Software Name: Vulkan SDK for Mac
    Download Link: https://vulkan.lunarg.com/sdk/home
    Version: 1.4.328.1
    Notes: As mentioned the windows and linux versions of this SDK have already been approved and the set of licenses appears to be very similar if not identical. There is a large number of licenses but if the previous reviewer of those TPS requests could be involved it would likely help accelerate the process.
        -->
<Location>The Mac SDK will be located within Engine/Source/ThirdParty/Vulkan and will initially be added to the //Fortnite/Main stream in Perforce.</Location>
<Function>The Vulkan SDK for Mac lets apps use the Vulkan graphics system on Macs, even though macOS doesn't support it directly. It does this through MoltenVK, which translates Vulkan's commands into Apple's Metal system. In short, it's a compatibility layer that allows games and graphics software written for Vulkan to run on macOS.</Function>
<Eula>https://vulkan.lunarg.com/license/#/release/record/7899465</Eula>
  <RedistributeTo>
    <EndUserGroup>Licensees</EndUserGroup>
    <EndUserGroup>P4</EndUserGroup>
    <EndUserGroup>Git</EndUserGroup>
  </RedistributeTo>
  <LicenseFolder>/Engine/Source/ThirdParty/Licenses</LicenseFolder>
</TpsData>
