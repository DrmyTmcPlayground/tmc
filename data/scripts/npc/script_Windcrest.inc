@ All Windcrests
SCRIPT_START script_Windcrest
	BeginBlock
	Call MakeCheckableObjectInteractable
	Call Windcrest_ChangeInteractableHitbox
	DoPostScriptAction 0x000a
	DoPostScriptAction 0x0007
	EndBlock
script_0800B024:
	CheckEntityInteractType
	JumpIfNot script_0800B024
	SetPlayerIdle
	Call DisablePauseMenuAndPutAwayItems
	CheckGlobalFlag EZLO_TALKS_AFTER_USING_OCARINA_FOR_FIRST_TIME
	JumpIf script_0800B082
	CheckGlobalFlag EZLO_TALKS_ABOUT_THE_FIRST_WIND_CREST
	JumpIf script_0800B064
	MessageNoOverlap TEXT_TINGLE, 0x32
	WaitUntilTextboxCloses
	DoPostScriptAction 0x0008
	Call Windcrest_Unlock
	Wait 0x003c
	EzloMessage TEXT_EZLO, 0x5c
	WaitUntilTextboxCloses
	SetGlobalFlag EZLO_TALKS_ABOUT_THE_FIRST_WIND_CREST
	Jump script_0800B09C
script_0800B064:
	MessageNoOverlap TEXT_TINGLE, 0x32
	WaitUntilTextboxCloses
	DoPostScriptAction 0x0008
	Call Windcrest_Unlock
	Wait 0x003c
	EzloMessage TEXT_EZLO, 0x5d
	WaitUntilTextboxCloses
	Jump script_0800B09C
script_0800B082:
	MessageNoOverlap TEXT_TINGLE, 0x32
	WaitUntilTextboxCloses
	DoPostScriptAction 0x0008
	Call Windcrest_Unlock
	Wait 0x003c
	EzloMessage TEXT_EZLO, 0x5e
	WaitUntilTextboxCloses
script_0800B09C:
	Call EnablePauseMenu
	EnablePlayerControl
	DoPostScriptAction 0x0006
SCRIPT_END
	.2byte 0x0000
