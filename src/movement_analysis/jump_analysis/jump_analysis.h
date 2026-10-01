#pragma once

#include "movement_analysis/player_context.h"

enum JumpType
{
	JumpType_FullInvalid = -1,
	JumpType_LongJump,
	JumpType_Bhop,
	JumpType_MultiBhop,
	JumpType_WeirdJump,
	JumpType_LadderJump,
	JumpType_Ladderhop,
	JumpType_Jumpbug,
	JumpType_Fall,
	JumpType_Other,
	JumpType_Invalid,
};

#define IGNORE_JUMP_TIME                (0.2f + 0.001f)
#define JS_EPSILON                      0.03125f
#define JS_MAX_BHOP_GROUND_TIME         0.05f
#define JS_MAX_DUCKBUG_RESET_TIME       0.05f
#define JS_MAX_NOCLIP_RESET_TIME        0.4f
#define JS_MAX_WEIRDJUMP_FALL_OFFSET    (64.0f + JS_EPSILON)
#define JS_TOUCH_GRACE_PERIOD           0.04f
#define JS_SPEED_MODIFICATION_TOLERANCE 0.1f
#define JS_TELEPORT_DISTANCE_SQUARED    4096.0f * 4096.0f * ENGINE_FIXED_TICK_INTERVAL *ENGINE_FIXED_TICK_INTERVAL
#define JS_MAX_STRAFES                  512
#define JS_MAX_AA_CALLS_PER_STRAFE      512
#define JS_MAX_AA_CALLS_PER_JUMP        4096

class Jump;

class AACall
{
public:
	f32 externalSpeedDiff {};
	f32 prevYaw {};
	f32 currentYaw {};
	f32 startFraction {};
	f32 endFraction {1.0f};
	Vector wishdir;
	f32 maxspeed {};
	f32 wishspeed {};
	f32 accel {};
	f32 surfaceFriction {};
	f32 duration {};
	u64 buttons[3] {};
	Vector velocityPre;
	Vector velocityPost;
	bool ducking {};

	f32 CalcIdealYaw(bool useRadians = false);
	f32 CalcMinYaw(bool useRadians = false);
	f32 CalcMaxYaw(bool useRadians = false);
	f32 CalcAccelSpeed(bool tryMaxSpeed = false);
	f32 CalcIdealGain();
};

class Strafe
{
public:
	Strafe() = default;

	Strafe(Jump *jump) : jump(jump) {}

	Jump *jump {};
	CCopyableUtlVector<AACall> aaCalls;
	TurnState turnstate {};
	f32 duration {};
	f32 badAngles {};
	f32 overlap {};
	f32 deadAir {};
	f32 syncDuration {};
	f32 width {};
	f32 airGain {};
	f32 maxGain {};
	f32 airLoss {};
	f32 collisionGain {};
	f32 collisionLoss {};
	f32 externalGain {};
	f32 externalLoss {};
	f32 strafeMaxSpeed {};

	void End();
	void UpdateCollisionVelocityChange(f32 delta);

	f32 GetStrafeDuration() const
	{
		return duration;
	}

	f32 GetMaxGain() const
	{
		return maxGain;
	}

	f32 GetGain() const
	{
		return airGain + collisionGain;
	}

	f32 GetWidth() const
	{
		return width;
	}

	f32 GetBadAngleDuration() const
	{
		return badAngles;
	}

	f32 GetOverlapDuration() const
	{
		return overlap;
	}

	f32 GetDeadAirDuration() const
	{
		return deadAir;
	}

	f32 GetSyncDuration() const
	{
		return syncDuration;
	}

	void UpdateStrafeMaxSpeed(f32 speed)
	{
		strafeMaxSpeed = MAX(strafeMaxSpeed, speed);
	}
};

class Jump
{
public:
	Jump() = default;

	Jump(CSVILKAPlayer *player) : player(player) {}

	CSVILKAPlayer *player {};
	u32 serverTick {};
	Vector takeoffOrigin;
	Vector adjustedTakeoffOrigin;
	Vector takeoffVelocity;
	Vector landingOrigin;
	Vector adjustedLandingOrigin;
	JumpType jumpType {JumpType_Invalid};
	JumpType originalJumpType {JumpType_Invalid};
	f32 totalDistance {};
	f32 currentMaxSpeed {};
	f32 currentMaxHeight {-16384.0f};
	f32 airtime {};
	f32 deadAir {};
	f32 overlap {};
	f32 badAngles {};
	f32 sync {};
	f32 width {};
	f32 gainEff {};
	bool hitHead {};
	bool valid {true};
	bool ended {};
	f32 release {};
	u32 aaCallCount {};
	CCopyableUtlVector<Strafe> strafes;
	f32 touchDuration {};
	char invalidateReason[256] {};
	bool trackingRelease {true};

	void Init();
	void UpdateAACallPost(Vector wishdir, f32 wishspeed, f32 accel);
	void Update();
	void End();
	Strafe *GetCurrentStrafe();

	void Invalidate(const char *reason)
	{
		valid = false;
		V_strncpy(invalidateReason, reason ? reason : "Invalid jump", sizeof(invalidateReason));
	}

	void MarkHitHead()
	{
		hitHead = true;
	}

	bool IsValid() const
	{
		return valid && jumpType >= JumpType_LongJump && jumpType <= JumpType_Fall;
	}

	bool AlreadyEnded() const
	{
		return ended;
	}

	bool DidHitHead() const
	{
		return hitHead;
	}

	JumpType GetJumpType() const
	{
		return jumpType;
	}

	f32 GetOffset() const
	{
		return adjustedLandingOrigin.z - adjustedTakeoffOrigin.z;
	}

	f32 GetDistance(bool useDistbugFix = true, bool disableAddDist = false, i32 floorLevel = -1);

	f32 GetSync() const
	{
		return sync;
	}

	f32 GetGainEfficiency() const
	{
		return gainEff;
	}
};

class JumpAnalysisService : public PlayerService
{
public:
	explicit JumpAnalysisService(CSVILKAPlayer *player) : PlayerService(player), jumps(1, 0) {}

	CUtlVector<Jump> jumps;
	f32 lastJumpButtonTime {};
	f32 lastNoclipTime {};
	f32 lastDuckbugTime {};
	f32 lastGroundSpeedCappedTime {};
	f32 lastMovementProcessedTime {};
	Vector tpmVelocity;
	bool possibleEdgebug {};
	bool aaCallPending {};
	f32 lastWPressedTime {};
	bool ladderHopThisMove {};

	void Reset() override;
	void OnProcessMovement();
	void OnProcessMovementPost();
	void OnChangeMoveType(MoveType_t oldMoveType);
	void OnTryPlayerMove();
	void OnTryPlayerMovePost();
	JumpType DetermineJumpType();
	f32 GetLastJumpRelease();
	bool HitBhop();
	bool HitDuckbugRecently();
	bool ValidWeirdJumpDropDistance();
	bool GroundSpeedCappedRecently();
	void AddJump();
	void UpdateJump();
	void EndJump();
	void HandleTeleport();
	void InvalidateJumpAnalysis(const char *reason = nullptr);
	void OnAirAccelerate();
	void OnAirAcceleratePost(Vector wishdir, f32 wishspeed, f32 accel);
	void TrackJumpVariables();
	void CheckValidMoveType();
	void DetectNoclip();
	void DetectEdgebug();
	void DetectInvalidCollisions();
	void DetectInvalidGains();
	void DetectExternalModifications();
	void DetectWater();
};
