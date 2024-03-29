#include <string.h>

#include "gba.h"
#include "game_states.h"
#include "bit_control.h"
#include "fixed.h"

#include "256Palette.h"
#include "sprites.h"

i32
WrapY(i32 y)
{
	if(y < 0)
	{
		y += OBJMAXY + 1;
	}

	return y;
}

i32
WrapX(i32 x)
{
	if(x < 0)
	{
		x += OBJMAXX + 1;
	}

	return x;
}

// return 0 if there is no collision, 1 otherwise
u16 PlayerCollideBorder(Player *player, ScreenDim *screenDim)
{
	u16 outOfBounds = 0;

	// check top
	if( player->y + player->bounding_box.y < screenDim->y ) {
		player->y = screenDim->y - player->bounding_box.y;
		player->velY = 0;
		outOfBounds = 1;
	}

	// check bottom
	//if( player->y + player->bounding_box.y + player->bounding_box.h > screenDim->h ) {
	//	player->y = screenDim->h - player->bounding_box.h - player->bounding_box.y;
	//	player->velY = 0;
	//	outOfBounds = 1;
	//}

	if( outOfBounds > 0 )
		return 1;
	return 0;
}

Player
Player_Create(u32 oamIdx, u32 x, u32 y, Rectangle bounding_box, fp_t velX, fp_t velY)
{
    Player player = {0};

    player.oamIdx = oamIdx;
    player.x = x;
    player.y = y;
    player.bounding_box = bounding_box;
    player.velX = velX;
    player.velY = velY;
    player.anim = Animation_Create(
            (u32[]){
                SPRITE_Robo_1_CHARNAME,
                SPRITE_Robo_2_CHARNAME,
                SPRITE_Robo_2_CHARNAME,
                SPRITE_Robo_3_CHARNAME,
                SPRITE_Robo_3_CHARNAME,
                SPRITE_Robo_4_CHARNAME,
                SPRITE_Robo_4_CHARNAME,
                SPRITE_Robo_5_CHARNAME
            }, //frame #s refer to attr2 charname
            8, 30, FALSE
    );

    return player;
}

void UpdateOBJPos(OBJ_ATTR *obj, int x, int y)
{
    BF_SET(&obj->attr1, x, ATTR1_XCOORD_LEN, ATTR1_XCOORD_SHIFT);
    BF_SET(&obj->attr0, y, ATTR0_YCOORD_LEN, ATTR0_YCOORD_SHIFT);
}

// Create a new Obstacle struct and setup its associated OAM OBJs
Obstacle
ObstacleCreate(
	OBJ_ATTR* OAM_objs,
	OBJPool* obstaclePool,
	xorshift32_state* randState
	)
{
    Obstacle Result = {0};

	Result.x = OBSTACLE_START_X;
    Result.gapSize = xorshift32_range(randState, 50, 96);
	Result.y = xorshift32_range(randState, Result.gapSize / 2 + 16, 160 - (Result.gapSize / 2) - 16);
    Result.active = 1;

    // calculate the number of tiles needed on the top, including the end tile
    // the division will truncate the number which means that we need to add 1 to make sure
    //     there's a tile that will reach the edge of the screen
    i32 numTilesToTopBorder = (Result.y - (Result.gapSize / 2)) / 32 + 1;
    i32 numTilesToBtmBorder = (SCREEN_HEIGHT - ((i32)Result.y + ((i32)Result.gapSize / 2))) / 32 + 1;

#ifdef __DEBUG__
    char debug_msg[DEBUG_MSG_LEN];
	mgba_printf(DEBUG_DEBUG, "Creating a new obstacle...");
	
	snprintf(debug_msg, DEBUG_MSG_LEN, "y: %ld, gap: %ld", Result.y, Result.gapSize);
	mgba_printf(DEBUG_DEBUG, debug_msg);

    snprintf(debug_msg, DEBUG_MSG_LEN, "Number top tiles: %ld", numTilesToTopBorder);
    mgba_printf(DEBUG_DEBUG, debug_msg);

    snprintf(debug_msg, DEBUG_MSG_LEN, "Number btm tiles: %ld", numTilesToBtmBorder);
    mgba_printf(DEBUG_DEBUG, debug_msg);
#endif

    i32 objPoolIdx = 0;

    for(u32 i = 0; i < numTilesToTopBorder + numTilesToBtmBorder; i++)
    {
        // setup the obstacle tile struct
        objPoolIdx = OBJPool_GetNextIdx(obstaclePool);
        Result.tiles[i].oamIdx = obstaclePool->indexes[objPoolIdx];
        Result.tiles[i].active = 1;

        // handle differences between top and btm tiles
        if(i < numTilesToTopBorder)
        {
            Result.tiles[i].y = Result.y - (Result.gapSize / 2) - (OBSTACLE_TILE_SIZE * (i + 1));
        }
        else {
            Result.tiles[i].y = Result.y + (Result.gapSize / 2) + (OBSTACLE_TILE_SIZE * (i - numTilesToTopBorder));
            BIT_SET(&OAM_objs[obstaclePool->indexes[objPoolIdx]].attr1, ATTR1_FLIPVERT);
        }

        // wrap the tile vertically if it goes out of bounds
		Result.tiles[i].y = WrapY(Result.tiles[i].y);

        // setup the obstacle sprite in OAM
        BF_SET(&OAM_objs[obstaclePool->indexes[objPoolIdx]].attr0, 1, 1, ATTR0_COLORMODE);
        BF_SET(&OAM_objs[obstaclePool->indexes[objPoolIdx]].attr1, 2, 2, ATTR1_OBJSIZE);
        BF_SET(&OAM_objs[obstaclePool->indexes[objPoolIdx]].attr1, Result.x, ATTR1_XCOORD_LEN, ATTR1_XCOORD_SHIFT);
        BF_SET(&OAM_objs[obstaclePool->indexes[objPoolIdx]].attr0, Result.y, ATTR0_YCOORD_LEN, ATTR0_YCOORD_SHIFT);
        // the first two tiles should be end-pieces
        if(i == 0 || i == numTilesToTopBorder)
        {
            // TODO: the starting tile is #16 in the tile set but the char name for
            // this sprite needs to be 32... why? what is a "char name" (attr2)?
            BF_SET(
                &OAM_objs[obstaclePool->indexes[objPoolIdx]].attr2,
                SPRITE_Obstacle_End_CHARNAME,
                ATTR2_CHARNAME_LEN,
                ATTR2_CHARNAME_SHIFT
            );
        }
        else
        {
            // every other tile but the first should use a tiling sprite
            BF_SET(
                &OAM_objs[obstaclePool->indexes[objPoolIdx]].attr2,
                SPRITE_Obstacle_Tile_01_CHARNAME,
                ATTR2_CHARNAME_LEN,
                ATTR2_CHARNAME_SHIFT
            );
        }
        BIT_CLEAR(&OAM_objs[obstaclePool->indexes[objPoolIdx]].attr0, ATTR0_DISABLE);
    }

    // create the bounding boxes for the obstacle
    Result.bounding_box_top = Rectangle_Create(0, -Result.y, 24, Result.y - Result.gapSize / 2);
    Result.bounding_box_btm = Rectangle_Create(0, Result.gapSize / 2, 24, SCREEN_HEIGHT - Result.y);

#ifdef __DEBUG__
	snprintf(debug_msg, DEBUG_MSG_LEN, "top: x: %ld, y: %ld, w: %ld, h: %ld", Result.bounding_box_top.x, Result.bounding_box_top.y, Result.bounding_box_top.w, Result.bounding_box_top.h);
	mgba_printf(DEBUG_DEBUG, debug_msg);

	snprintf(debug_msg, DEBUG_MSG_LEN, "btm: x: %ld, y: %ld, w: %ld, h: %ld", Result.bounding_box_btm.x, Result.bounding_box_btm.y, Result.bounding_box_btm.w, Result.bounding_box_btm.h);
	mgba_printf(DEBUG_DEBUG, debug_msg);
#endif


    return Result;
}

void OAM_OBJClear(i32 idx)
{
#if __DEBUG__
    char debug_msg[DEBUG_MSG_LEN];
    snprintf(debug_msg, DEBUG_MSG_LEN, "clearing OAMOBJ[%ld]", idx);
    mgba_printf(DEBUG_DEBUG, debug_msg);
#endif
    // zero out the given OAM OBJ and then disable it
	OBJ_ATTR *obj = (OBJ_ATTR *)OAM_MEM;
    obj[idx] = (OBJ_ATTR){0};

    BIT_SET(&OAM_MEM[idx], ATTR0_DISABLE);
}

void Obstacle_Clear(Obstacle* obstacle)
{
    // iterate over each of the Obstacle's tiles and disable them
    for(size_t i = 0; i < ARR_LENGTH(obstacle->tiles); i++)
    {
        if(obstacle->tiles[i].active)
        {
            OAM_OBJClear(obstacle->tiles[i].oamIdx);
            obstacle->tiles[i].active = 0;
        }
    }
}



GameStates
gameState_SplashScreenInit(SplashScreenState *state)
{
    return GAMESTATE_SPLASHSCREEN;
}

GameStates
gameState_SplashScreen(SplashScreenState *state)
{
	return GAMESTATE_SPLASHSCREENDEINIT;
}

GameStates
gameState_GameInit(GameScreenState *state)
{

#if __DEBUG__
    char debug_msg[DEBUG_MSG_LEN];
#endif

	// Initialize display control register
	*REG_DISPCNT = 0;
	BIT_CLEAR(REG_DISPCNT, DISPCNT_BGMODE_SHIFT); // set BGMode 0
	BIT_SET(REG_DISPCNT, DISPCNT_BG0FLAG_SHIFT); // turn on BG0
	BIT_SET(REG_DISPCNT, DISPCNT_OBJMAPPING_SHIFT); // 1D mapping
	BIT_SET(REG_DISPCNT, DISPCNT_OBJFLAG_SHIFT); // show OBJs

	// Initialize BG0's attributes
	u32 bgMapBaseBlock = 28;
	u32 bgCharBaseBlock = 0;
	*BG0CNT = 0;
	BIT_SET(BG0CNT, BGXCNT_COLORMODE); // 256 color palette
	BF_SET(BG0CNT, bgCharBaseBlock, 2, BGXCNT_CHARBASEBLOCK);  // select bg tile base block
	BF_SET(BG0CNT, bgMapBaseBlock, 5, BGXCNT_SCRNBASEBLOCK); // select bg map base block
	BF_SET(BG0CNT, 2, 2, BGXCNT_SCREENSIZE); // select map size (64x32 tiles, or 2x 32x32-tile screens, side-by-side)

    // setup important scene items
	state->inputs = (InputState){0};
	state->screenDim = (ScreenDim){ 0, 0, 240, 160 };
    state->player = Player_Create(4, 60, 80, Rectangle_Create(8, 9, 21, 14), 0, 0);
    state->obstaclePool = OBJPool_Create(104, 24); // 6 tiles per obstacle, 4 obstacles in use at a time
	state->frameCounter = 1;
    state->score = 0;
    state->GravityPerFrame = FP(0, 0x4000);
    state->aButtonAnimation = Animation_Create(
        (u32[]){
            SPRITE_ButtonA_dark_CHARNAME,
            SPRITE_ButtonA_light_CHARNAME
        },
        2, 8, FALSE
    );

    Animation_Play(state->player.anim);

	// setup the random number generator
    state->randState = (xorshift32_state){69420};

	// copy the palette data to the BG and OBJ palettes
	memcpy(BGPAL_MEM, Pal256, PalLen256);
	memcpy(OBJPAL_MEM, Pal256, PalLen256);

    // copy all sprite data into VRAM
    // compiled using tile-builder, extra metadata in `sprites.h`
	// TODO: see about timing the memcpy
	// 		 look into using DMA transfers for potential speedup
	memcpy(&tile8_mem[4][0], SpriteTiles, SPRITETILES_LEN);

    // initialize OAM items
	OBJ_ATTR *OAM_objs = (OBJ_ATTR *)OAM_MEM;
	OAM_Init();

    // TODO: make it easier to get OAM indices instead of hardcoding them
    // setup the score counter sprites
    state->scoreCounterOAMIdxs[0] = 0;
    state->scoreCounterOAMIdxs[1] = 1;
    state->scoreCounterOAMIdxs[2] = 2;
    state->scoreCounterOAMIdxs[3] = 3;
    for(u32 i = 0; i < ARR_LENGTH(state->scoreCounterOAMIdxs); i++)
    {
        BIT_SET(&OAM_objs[state->scoreCounterOAMIdxs[i]].attr0, ATTR0_COLORMODE);
        BF_SET(&OAM_objs[state->scoreCounterOAMIdxs[i]].attr1, SPRITE_Numbers_0_OBJSIZE, ATTR1_OBJSIZE_LEN, ATTR1_OBJSIZE);
        BF_SET(&OAM_objs[state->scoreCounterOAMIdxs[i]].attr2, SPRITE_Numbers_0_CHARNAME, ATTR2_CHARNAME_LEN, ATTR2_CHARNAME_SHIFT);
        BF_SET(&OAM_objs[state->scoreCounterOAMIdxs[i]].attr1, 5 + i * 16, ATTR1_XCOORD_LEN, ATTR1_XCOORD_SHIFT);
        BF_SET(&OAM_objs[state->scoreCounterOAMIdxs[i]].attr0, 5, ATTR0_YCOORD_LEN, ATTR0_YCOORD_SHIFT);
        BIT_CLEAR(&OAM_objs[state->scoreCounterOAMIdxs[i]].attr0, ATTR0_DISABLE);
    }

	// setup the robot's sprite
	BIT_SET(&OAM_objs[state->player.oamIdx].attr0, ATTR0_COLORMODE);
	BF_SET(&OAM_objs[state->player.oamIdx].attr1, state->player.x, ATTR1_XCOORD_LEN, ATTR1_XCOORD_SHIFT);
	BF_SET(&OAM_objs[state->player.oamIdx].attr0, state->player.y, ATTR0_YCOORD_LEN, ATTR0_YCOORD_SHIFT);
	BIT_CLEAR(&OAM_objs[state->player.oamIdx].attr0, ATTR0_DISABLE);
	BF_SET(&OAM_objs[state->player.oamIdx].attr1, SPRITE_Robo_1_OBJSIZE, 2, ATTR1_OBJSIZE);
	BF_SET(&OAM_objs[state->player.oamIdx].attr2, state->player.anim->curFrame, ATTR2_CHARNAME_LEN, ATTR2_CHARNAME_SHIFT);

	// setup the title screen button
    // TODO: make it easier to identify which OAM OBJ to modify (instead of using numbers)
	BIT_SET(&OAM_objs[5].attr0, ATTR0_COLORMODE);
	BF_SET(&OAM_objs[5].attr1, 160, ATTR1_XCOORD_LEN, ATTR1_XCOORD_SHIFT);
	BF_SET(&OAM_objs[5].attr0, 65, ATTR0_YCOORD_LEN, ATTR0_YCOORD_SHIFT);
	BIT_CLEAR(&OAM_objs[5].attr0, ATTR0_DISABLE);
	BF_SET(&OAM_objs[5].attr1, SPRITE_ButtonA_light_OBJSIZE, 2, ATTR1_OBJSIZE);
	BF_SET(&OAM_objs[5].attr2, SPRITE_ButtonA_light_CHARNAME, ATTR2_CHARNAME_LEN, ATTR2_CHARNAME_SHIFT);

#if __DEBUG__
    // test the OBJPool system by making sure the index numbers
    // wrap properly within the pool and also by filling the
    // OAM objs using the pool
    //for(size_t i = 0; i < 32; i++)
    //{
    //    i32 poolIdx = OBJPool_GetNextIdx(&state->obstaclePool);
    //    i32 objIdx = state->obstaclePool.indexes[poolIdx];
    //    snprintf(debug_msg, DEBUG_MSG_LEN,
    //            "obstaclePool[%ld]: %ld",
    //            poolIdx, objIdx);
    //    mgba_printf(DEBUG_DEBUG, debug_msg);
    //    BIT_SET(&OAM_objs[objIdx].attr0, ATTR0_COLORMODE);
    //    BF_SET(&OAM_objs[objIdx].attr1, 2, 2, ATTR1_OBJSIZE);
    //    BF_SET(&OAM_objs[objIdx].attr2, 0, ATTR2_CHARNAME_LEN, ATTR2_CHARNAME_SHIFT);
    //}
    debug_msg[0] = '\0';
    debug_msg[0] = debug_msg[0]; // shut up unused var warning
#endif

	// dummy BG tile art
	// the number corresponds to the color idx in the palette
	char smileyTile[64] = {
		55, 55, 55, 55, 55, 55, 55, 55,
		55, 55, 55, 55, 55, 55, 55, 55,
		55, 55,  1, 55, 55,  1, 55, 55,
		 1, 55,  1, 55, 55,  1, 55,  1,
		 1, 55, 55, 55, 55, 55, 55,  1,
		55,  1, 55, 55, 55, 55,  1, 55,
		55, 55,  1,  1,  1,  1, 55, 55,
		55, 55, 55, 55, 55, 55, 55, 55,
	};
	memcpy(&tile8_mem[0][2], &smileyTile, 64);
	char blankTile[64] = {
		55, 55, 55, 55, 55, 55, 55, 55,
		55, 55, 55, 55, 55, 55, 55, 55,
		55, 55, 55, 55, 55, 55, 55, 55,
		55, 55, 55, 55, 55, 55, 55, 55,
		55, 55, 55, 55, 55, 55, 55, 55,
		55, 55, 55, 55, 55, 55, 55, 55,
		55, 55, 55, 55, 55, 55, 55, 55,
		55, 55, 55, 55, 55, 55, 55, 55
	};
	memcpy(&tile8_mem[0][1], &blankTile, 64);

	// Create a basic tilemap
	BG_TxtMode_Tile blankTileIdx = 1; // first non-zero BG tile
	*BG_TxtMode_Screens[0] = 0;
	for(size_t i = 0; i < 2; i++)
	{
		for(size_t j = 0; j < 1024; j++)
		{
			BG_TxtMode_Screens[bgMapBaseBlock + i][j] = blankTileIdx;
		}
	}
	BG_TxtMode_Screens[bgMapBaseBlock][0] = 2;

#ifdef __DEBUG__
	// print a debug message, viewable in mGBA
	mgba_printf(DEBUG_DEBUG, "Hey GameBoy");
#endif

	state->bgHOffset = 0;
	state->bgHOffsetRate = FP(0, 0x4000);

	// create an obstacle
    for(u32 i = 0; i < OBSTACLES_MAX; i++)
    {
        state->obstacles[i] = (Obstacle){0};
    }
    state->obstacleIdx = 0;
    state->obstacles[state->obstacleIdx] = ObstacleCreate(OAM_objs, &state->obstaclePool, &state->randState);
	state->obstacleIdx++;

	return GAMESTATE_TITLESCREEN;
}

GameStates
gameState_TitleScreen(GameScreenState *state)
{
    Vsync();
    UpdateButtonStates(&state->inputs);

	// scroll BG. since the BGxHOFS register is write-only,
	// the offset needs to be stored in the game state
    state->bgHOffset += state->bgHOffsetRate;
    if(state->bgHOffset > FP(511,0)) { state->bgHOffset -= FP(511,0); }
    *BG0HOFS = FP2Int(state->bgHOffset);

	OBJ_ATTR *OAM_objs = (OBJ_ATTR *)OAM_MEM;

    // ButtonA_light
	//BF_SET(&OAM_objs[5].attr2, SPRITE_ButtonA_light_CHARNAME, ATTR2_CHARNAME_LEN, ATTR2_CHARNAME_SHIFT);

    // move player
    state->player.velY += state->GravityPerFrame;
    if(state->player.y > 85)
    {
        state->player.velY = Int2FP(-3);
        xorshift32(&state->randState); // seed the RNG on button press
        Animation_Restart(state->aButtonAnimation);
        Animation_Restart(state->player.anim);
    }
    state->player.y += FP2Int(state->player.velY);
    Animation_Update(state->player.anim, 1);

    // udpate the A Button sprite
    Animation_Update(state->aButtonAnimation, 1);
    BF_SET(&OAM_objs[5].attr2, state->aButtonAnimation->frames[state->aButtonAnimation->curFrame], ATTR2_CHARNAME_LEN, ATTR2_CHARNAME_SHIFT);
	
    // update the player sprite
    BF_SET(
        &OAM_objs[state->player.oamIdx].attr2,
        state->player.anim->frames[state->player.anim->curFrame],
        ATTR2_CHARNAME_LEN,
        ATTR2_CHARNAME_SHIFT
        );
    UpdateOBJPos(
        &OAM_objs[state->player.oamIdx],
        state->player.x,
        WrapY(state->player.y)
        ); 

	if(ButtonPressed(&state->inputs, KEYPAD_A))
	{
		BIT_SET(&OAM_objs[5].attr0, ATTR0_DISABLE);
		return GAMESTATE_GAMESCREEN;
	}

	return GAMESTATE_TITLESCREEN;
}

GameStates
gameState_GameScreen(GameScreenState *state)
{
    char debug_msg[DEBUG_MSG_LEN];

    Vsync();
    UpdateButtonStates(&state->inputs);

	OBJ_ATTR *OAM_objs = (OBJ_ATTR *)OAM_MEM;

    // scroll the BG
    // BG0HOFS is write-only so we need an extra variable (bgHOffset)
    // to keep track of where the BG should be and assign that to
    // the register
    state->bgHOffset += state->bgHOffsetRate;
    if(state->bgHOffset > FP(511,0)) { state->bgHOffset -= FP(511,0); }
    *BG0HOFS = FP2Int(state->bgHOffset);

    // move player
    state->player.velY += state->GravityPerFrame;
    if(ButtonPressed(&state->inputs, KEYPAD_A))
    {
        state->player.velY = Int2FP(-3);
        xorshift32(&state->randState); // seed the RNG on button press
        Animation_Restart(state->player.anim);
    }
    state->player.y += FP2Int(state->player.velY);
    Animation_Update(state->player.anim, 1);
    PlayerCollideBorder(&state->player, &state->screenDim);

    // update the player sprite
    BF_SET(
        &OAM_objs[state->player.oamIdx].attr2,
        state->player.anim->frames[state->player.anim->curFrame],
        ATTR2_CHARNAME_LEN,
        ATTR2_CHARNAME_SHIFT
        );
    UpdateOBJPos(
        &OAM_objs[state->player.oamIdx],
        state->player.x,
        WrapY(state->player.y)
        ); 

    // check if the player has gone off the btm of the screen
    if(state->player.y + state->player.bounding_box.y > state->screenDim.h)
    {
        //game over
        return GAMESTATE_GAMEOVER;
    }

    // update the obstacles
    for(size_t i = 0; i < OBSTACLES_MAX; i++)
    {
        if(state->obstacles[i].active == 0) continue;

        state->obstacles[i].x -= 1;

        // add to the score if the obstacle has moved past the player
        if(
            (state->obstacles[i].x + state->obstacles[i].bounding_box_top.w < state->player.x) &&
            state->obstacles[i].countedScore == 0)
        {
            if(state->score < 9999) ++state->score;
            state->obstacles[i].countedScore = 1;

            // redraw the score sprites
            u32 digit;
            u32 tmp = state->score;
            for(i32 i = ARR_LENGTH(state->scoreCounterOAMIdxs) - 1; i > -1; --i)
            {
                digit = tmp % 10;
                tmp /= 10;
                // the tile idx for the numbers starts at 128
                // tile idx goes up by 8 for each number
                BF_SET(
                    &OAM_objs[state->scoreCounterOAMIdxs[i]].attr2,
                    SPRITE_Numbers_0_CHARNAME + digit * 8, // TODO: magic number bad
                    ATTR2_CHARNAME_LEN,
                    ATTR2_CHARNAME_SHIFT
                );
            }

#ifdef __DEBUG__
            snprintf(debug_msg, DEBUG_MSG_LEN, "score: %ld", state->score);
            mgba_printf(DEBUG_DEBUG, debug_msg);
#endif
        }

        // check if the obstacle has gone out of the game
        if(state->obstacles[i].x <= -32)
        {
            // disable the obstacle
            Obstacle_Clear(&state->obstacles[i]);
            state->obstacles[i] = (Obstacle){0};
        }

        // in order to get the obstacle to move off the left side,
        // the obj must wrap back in from the right side of the map
        for(i32 j = 0; j < MAX_TILES_LEN; j++)
        {
            if(state->obstacles[i].tiles[j].active)
            {
                UpdateOBJPos(
                        &OAM_objs[state->obstacles[i].tiles[j].oamIdx],
                        WrapX(state->obstacles[i].x),
                        state->obstacles[i].tiles[j].y
                        );
            }
        }
    }

    // check for collisions
    for(u32 i = 0; i < ARR_LENGTH(state->obstacles); i++)
    {
        // create a screen-space rectangle for the player
        Rectangle playerRect = Rectangle_Create(
                state->player.x + state->player.bounding_box.x,
                state->player.y + state->player.bounding_box.y,
                state->player.bounding_box.w,
                state->player.bounding_box.h);

        // create screen-space rects for the top and bottom bounding boxes
        if(state->obstacles[i].active)
        {
            Rectangle obstacleRectTop = Rectangle_Create(
                    state->obstacles[i].x + state->obstacles[i].bounding_box_top.x,
                    state->obstacles[i].y + state->obstacles[i].bounding_box_top.y,
                    state->obstacles[i].bounding_box_top.w,
                    state->obstacles[i].bounding_box_top.h);
            Rectangle obstacleRectBtm = Rectangle_Create(
                    state->obstacles[i].x + state->obstacles[i].bounding_box_btm.x,
                    state->obstacles[i].y + state->obstacles[i].bounding_box_btm.y,
                    state->obstacles[i].bounding_box_btm.w,
                    state->obstacles[i].bounding_box_btm.h);

            if( CheckCollision_RectRect(playerRect, obstacleRectTop) || 
                    CheckCollision_RectRect(playerRect, obstacleRectBtm)
              )
            {
#ifdef __DEBUG__
                mgba_printf(DEBUG_DEBUG, "GAME OVER");
#endif
                return GAMESTATE_GAMEOVER;
            }
        }
    }

    // compare the frameCounter variable. Once it reaches a certain
    // number, spawn a new obstacle
    if(state->frameCounter % 120 == 0)
    {
#ifdef __DEBUG__
        mgba_printf(DEBUG_DEBUG, "create new obstacle");
#endif
        state->obstacles[state->obstacleIdx] = ObstacleCreate(OAM_objs, &state->obstaclePool, &state->randState);
        state->obstacleIdx++;
        if(state->obstacleIdx == OBSTACLES_MAX) { state->obstacleIdx = 0; }
    }

    state->frameCounter++;


    return GAMESTATE_GAMESCREEN;
}

GameStates
gameState_GameOver(GameScreenState *state)
{
    // TODO: check/save the highscore

	return GAMESTATE_GAMESCREENDEINIT;
}

GameStates
gameState_GameScreenDeinit(GameScreenState *state)
{
	return GAMESTATE_GAMEINIT;
}

