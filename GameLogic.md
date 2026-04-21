# 3D Cyber Survivor Arena: Logic Specification

1. Core Architecture & Loop
   The game is a top-down 3D infinite survival shooter on a fixed circular XZ plane.
   • Termination Condition: player_hp <= 0.
   • Execution Order: Input → Movement → Spawn Check → Auto-Attack Check → Enemy AI → Projectile Physics → Collision Resolution → State Update.
2. Player Mechanics
   • Attributes: HP (100), Speed (8u/s), Attack Interval (0.18s), Radius (0.65).
   • Movement: 8-way WASD on XZ plane; vectors must be normalized to prevent diagonal speed boosts.
   • Combat: Auto-targets the nearest enemy within 360°. If no enemy is present, firing is suppressed.
3. Enemy AI & Spawning
   • Behavior: Direct pathing toward player position: V.enemy = normalize(P.player - P.enemy).
   • Spawning: Occurs at the arena perimeter.
   • Spawn Rate: Dynamic interval: max(0.25, 0.9 - time \* 0.006).
   • Population Control: Hard cap of 150 enemies active simultaneously.
4. Difficulty Scaling (Linear over Time)
   As survival_time increases, variables scale as follows:
   • Spawn Interval: Decreases (Calculated per formula in Sec 3).
   • Enemy Speed: base_speed + (time \* 0.0015)(Cap: 6u/s).
   • Enemy HP: base_hp + (time\*0.4).
5. Combat & Physics Systems
   • Projectiles: Linear trajectory, fixed speed, fixed lifetime. Single-hit (non-piercing).
   • Collision Matrix:
   • Projectile vs. Enemy: Circle-to-circle check. On hit: Damage enemy, destroy projectile.
   • Enemy vs. Player: Continuous contact damage (HP - (DPS \*dt))
   • Enemy vs. Enemy: Soft-body separation rule; overlapping enemies apply a "push" force to prevent stacking.
6. Environment & UI
   • Arena: Circular boundary. Players are restricted; projectiles/enemies are culled if they exit bounds.
   • Camera: Fixed tilt/rotation; position strictly locked to player_position.
   • State Management:
   • Death: Freeze logic, display survival time and kill count.
   • Reset: Key "R" reinitializes all variables and clears arrays.
   AI Implementation Note: When coding this, prioritize the Nearest Enemy Search algorithm and the Spawn Interval Formula as these dictate the game's primary feel and difficulty curve.

## Resource Structure

Game assets are stored in resources/object/ and organized by gameplay entity.

resources/object/
├── arena/      → Main arena environment (GLTF scene + textures)
├── mixamo/     → Player character "Vanguard" model and animations
├── staff/      → Player weapon (magic cyber staff model)
└── cube.obj    → Simple cube used for debugging (enemies, projectiles)

Arena provides the playable map and environment visuals.
Mixamo/Vanguard is the main player character with skeletal animations.
Staff is the player’s weapon used to fire projectiles.
Cube is a placeholder mesh used during development for enemies and bullets.
