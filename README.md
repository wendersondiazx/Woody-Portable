# Woody Portable — PSP Native Engine

**An experimental native game engine bringing the mechanics and 3D platforming experience of Woody Woodpecker: Escape from Buzz Buzzard Park
 to the PlayStation Portable.**

![Status](https://img.shields.io/badge/Status-In%20Development-orange?style=for-the-badge)
![Platform](https://img.shields.io/badge/Platform-PSP-blue?style=for-the-badge)
![Build](https://img.shields.io/badge/Build-Beta-green?style=for-the-badge)

---
> [!IMPORTANT]
> **Hardware Compatibility Notice**
>
> This port is primarily designed and optimized for PlayStation Portable models equipped with **64 MB of RAM, specifically the PSP-2000 and PSP-3000.**
>
> Due to the **PSP-1000's limited 32 MB of RAM**, this model may experience significant performance limitations, increased loading times, reduced stability, or compatibility issues.
>
> While support for the PSP-1000 is not entirely ruled out, **the PSP-2000 and PSP-3000** are strongly recommended for the best possible experience.


## Overview

**Woody Portable** is an independent experimental project focused on developing a native game engine capable of recreating the core 3D platforming mechanics, level structure, and gameplay experience of *Woody Woodpecker: Escape from Buzz Buzzard Park* on original PlayStation Portable hardware.

Inspired by custom homebrew engine adaptations, the engine follows a modular logical structure, using original game assets as a foundation for adapting and executing custom gameplay systems.

Unlike traditional full-game ports, this project does not aim to reproduce the complete original storyline or full cinematic sequences immediately.

Instead, its primary objective is to develop a specialized engine capable of loading level assets, implementing physics and platformer mechanics, and delivering a smooth, playable single-player experience while respecting the PSP's hardware constraints.

The project has been under development for approximately **six months** and has recently reached an important milestone: **a stable, playable experimental build**.

## Project Vision

The goal is not simply to make *Escape from Buzz Park* run on a PSP.

The goal is to explore how far the original hardware can be pushed through careful software engineering, efficient memory management, and optimized rendering techniques tailored for 3D platformers.

The project focuses on three main areas:

- **Native execution:** Running directly on real PSP hardware without relying on external emulation.
- **Asset compatibility:** Investigating methods for loading, interpreting, and adapting 3D meshes, textures, and animations.
- **Rendering & physics optimization:** Developing efficient rendering, collision detection, and resource-management techniques suitable for the PSP hardware.

## Engine Architecture

The engine is being designed around a modular approach, separating responsibilities between rendering, asset management, and platformer physics.

Its conceptual structure includes:

| Component | Purpose |
|---|---|
| Core Engine | Main execution flow and system management |
| Asset Management | Loading and interpreting levels, textures, and sound resources |
| Rendering System | Optimized 3D level geometry and character rendering |
| Gameplay Systems | Player controls, jump physics, collision, and enemy behavior |
| Resource Management | RAM usage and texture streaming optimization |

These components represent the project's architectural direction and may evolve as development progresses.

## Rendering and Performance

The PSP has distinct hardware constraints compared to home consoles and PCs of the era.

As a result, the engine prioritizes performance-oriented development rather than unoptimized direct asset rendering.

The research and development scope includes:

- Efficient level segment loading and VRAM allocation.
- Frustum culling and visibility-based optimizations.
- Dynamic texture compression and mipmapping.
- Custom collision detection algorithms optimized for 3D platforming.
- Balancing visual fidelity with a stable performance target.

The ultimate objective is to maintain responsive controls and smooth platforming while preserving the charming aesthetic of Buzz Park.

## Project Scope

This project is **an independent technical engine experiment**.

It focuses on creating a solid foundation for 3D platforming, asset compatibility, and homebrew experimentation on the PSP.

It is an independent fan-made project and is not affiliated with, endorsed by, or associated with Universal Interactive, Konami, NBCUniversal, or Sony Interactive Entertainment.

*Woody Woodpecker*, *Escape from Buzz Park*, PlayStation, and PSP are trademarks of their respective owners.

The project does not include or authorize the distribution of copyrighted commercial game assets or game ISOs.

## Current Development Stage

After approximately six months of development, the project has progressed from an experimental concept to a playable native PSP engine prototype.

The successful execution of level rendering, character controls, and platforming physics on physical PSP hardware is one of its most significant milestones so far.

There is still considerable engineering work ahead, particularly in physics tuning, graphics optimization, and audio decoding.

Nevertheless, the initial results demonstrate the technical potential of the approach.

---

# COMING SOON

**A new 3D platforming experiment on the original PlayStation Portable.**

*Built for real hardware. Driven by experimentation. Inspired by Woody Woodpecker.*

**Development is ongoing. Stay tuned for future updates, gameplay demonstrations, and technical milestones.**
