# IoStore Dependency Viewer

A lightweight standalone tool for viewing and navigating asset dependencies in Unreal Engine cooked builds using IoStore container files.

## Overview

The IoStore Dependency Viewer is a fast, standalone program that loads IoStore container files (`.utoc`, `.uondemandtoc`, `.umeta`) from cooked builds and provides an interactive interface to:

- Browse all assets/chunks in the containers
- Search for assets by name, chunk ID, or container
- View dependency trees for any asset
- Explore both dependencies and referencers
- Inspect asset metadata (size, compression, partition, etc.)
- Query chunk IDs and tags

## Key Features

- **No Editor Required**: Runs as a standalone program - much faster than launching the full editor
- **IoStore Native**: Directly reads `.utoc` and `.uondemandtoc` files from cooked builds
- **Filename Resolution**: Loads `.umeta` files to resolve chunk IDs to human-readable filenames
- **Dependency Analysis**: Shows package-level dependencies extracted from container headers
- **Fast Search**: Quickly filter thousands of assets by name, chunk ID, or container
- **Dual-View Interface**: Asset list and dependency tree work together for efficient navigation

## Usage

### Running the Tool

```bash
# From command line with auto-load
IoStoreDependencyViewer.exe -Path="D:\Fortnite4\CookedBuild\WindowsClient\FortniteGame\Content\Paks"

# Or run without arguments to use the GUI file browser
IoStoreDependencyViewer.exe
```

### Loading Data

1. Click **Load Directory** in the toolbar
2. Navigate to your cooked build Paks directory (e.g., `D:\Fortnite4\CookedBuild\WindowsClient\FortniteGame\Content\Paks`)
3. The tool will automatically scan for:
   - `.utoc` files (standard IoStore containers)
   - `.uondemandtoc` files (on-demand/streaming containers)
   - `.umeta` files (filename metadata)

### Viewing Dependencies

1. Select an asset from the list on the left
2. The dependency tree appears on the right showing:
   - **Metadata**: Chunk ID, size, compression info, partition
   - **Dependencies**: Packages this asset depends on
   - **Referencers**: Packages that depend on this asset

### Search

Use the search box to filter assets by:
- Filename
- Container name
- Chunk ID
- Tags (if present)

## Technical Details

### File Types

- **`.utoc`**: Standard IoStore Table of Contents (metadata for `.ucas` data files)
- **`.ucas`**: IoStore Container Archive Storage (actual binary asset data)
- **`.uondemandtoc`**: On-Demand IoStore TOC (for cloud streaming/on-demand delivery)
- **`.umeta`**: IoStore Container Metadata (maps chunk IDs to filenames)

### Architecture

The tool uses Unreal's existing IoStore APIs:

- **FIoStoreReader**: Loads and parses `.utoc` files
- **FOnDemandToc**: Loads and parses `.uondemandtoc` files
- **FIoContainerMetaReader**: Loads `.umeta` files for filename resolution
- **FIoContainerHeader**: Extracts package dependencies from container headers
- **Slate UI**: Provides the graphical interface

### Dependencies Tracked

The tool extracts dependencies from:
- **FFilePackageStoreEntry.ImportedPackages**: Direct package-to-package dependencies
- **Container Headers**: Parsed from the special ContainerHeader chunk in each container
- **Soft Package References**: References that may or may not be loaded

## Comparison to Asset Registry Viewer

| Feature | IoStore Dependency Viewer | Asset Registry Viewer |
|---------|---------------------------|----------------------|
| **Source Data** | Cooked build `.utoc` files | Asset registry `.bin` files |
| **Use Case** | Analyze shipped/cooked builds | Analyze project during development |
| **Startup Speed** | Fast (standalone) | Slow (requires editor) |
| **Dependency Info** | Package-level from containers | Full asset-level from registry |
| **Filename Resolution** | Via `.umeta` files | Built into registry |
| **Best For** | Shipping/cooked build analysis | Development/editor workflows |

## Configuration

The tool can be configured via the `Engine.ini` file. Add settings under the `[IoStoreDependencyViewer]` section:

```ini
[IoStoreDependencyViewer]
; Maximum number of containers to load concurrently (default: 20)
; Lower values reduce memory/CPU usage, higher values speed up loading
; Valid range: 1-100
MaxConcurrentContainerLoads=20
```

**MaxConcurrentContainerLoads**: Controls how many container files are loaded in parallel. When loading builds with many containers (100+), this prevents overwhelming the system by limiting concurrent zen.exe requests and file I/O operations. Adjust based on your system specs:
- **High-end systems** (32+ GB RAM, fast SSD): 30-50
- **Mid-range systems** (16 GB RAM): 20 (default)
- **Low-end systems** (8 GB RAM): 10-15

## Notes

- This tool does NOT modify any engine or game code
- All files are located in `/Engine/Source/Programs/IoStoreDependencyViewer/`
- The tool reuses existing IoStore loading code for reliability
- Works with any Unreal Engine project that uses IoStore containers
- Supports both standard containers and on-demand/streaming containers

## Building

```bash
# Regenerate project files
cd D:\Fortnite4
GenerateProjectFiles.bat

# Build with Visual Studio or command line
# Target: IoStoreDependencyViewer
# Configuration: Development
```

The compiled executable will be in:
```
D:\Fortnite4\Engine\Binaries\Win64\IoStoreDependencyViewer.exe
```
