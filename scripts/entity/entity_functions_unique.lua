-- OPENTOMB ENTITY FUNCTIONS SCRIPT
-- By TeslaRus, Lwmte, 2014-2016

----------------------------------------------
-------------------    TR 1  -----------------
----------------------------------------------

function tallblock_init(id)    -- Tall moving block (TR1)

    setEntityTypeFlag(id, ENTITY_TYPE_GENERIC);
    setEntityActivity(id, false);
	
    entity_funcs[id].distance_passed = 0;
    
    entity_funcs[id].onActivate = function(object_id, activator_id)
        if(getEntityEvent(object_id) == 0) then
            setEntityActivity(object_id, true);
            playSound(64, object_id);
            return ENTITY_TRIGGERING_ACTIVATED;
        end;
        return ENTITY_TRIGGERING_NOT_READY;
    end
    
	entity_funcs[id].onDeactivate = function(object_id, activator_id)
        if(getEntityEvent(object_id) ~= 0) then
            setEntityActivity(object_id, true);
            playSound(64, object_id);
            return ENTITY_TRIGGERING_DEACTIVATED;
        end;
        return ENTITY_TRIGGERING_NOT_READY;
    end
    
    entity_funcs[id].onLoop = function(object_id)
        local move_dist = 32.0 * 60.0 * frame_time;
        if(getEntityEvent(object_id) == 0) then 
            move_dist = 0.0 - move_dist;
        end;
        
        entity_funcs[object_id].distance_passed = entity_funcs[object_id].distance_passed + move_dist;
        moveEntityLocal(object_id, 0.0, move_dist, 0.0);
        if(math.abs(entity_funcs[object_id].distance_passed) >= 2048.0) then
            stopSound(64, object_id);
            setEntityActivity(object_id, false);
            entity_funcs[object_id].distance_passed = 0;
        end;
    end
    
    entity_funcs[id].onDelete = function(object_id)
        entity_funcs[object_id].distance_passed = nil;
    end
    
    prepareEntity(id);
end


function midastouch_init(id)    -- Midas gold touch

    setEntityTypeFlag(id, ENTITY_TYPE_GENERIC);
    
    entity_funcs[id].onLoop = function(object_id)
        if(getEntityDistance(player, object_id) < 1024.0) then
            local lara_anim, frame, count = getEntityAnim(player, ANIM_TYPE_BASE);
            local lara_sector = getEntitySectorIndex(player);
            local hand_sector = getEntitySectorIndex(object_id);
            
            if((lara_sector == hand_sector) and (getEntityMoveType(player) == MOVE_ON_FLOOR) and (lara_anim ~= 50)) then
                setCharacterParam(player, PARAM_HEALTH, 0);
                entitySSAnimEnsureExists(player, ANIM_TYPE_MISK_1, 5); --ANIM_TYPE_MISK_1 - add const
                setEntityAnim(player, ANIM_TYPE_MISK_1, 1, 0);
                entitySSAnimSetEnable(player, ANIM_TYPE_MISK_1, 1);
                entitySSAnimSetEnable(player, ANIM_TYPE_BASE, 0);
                disableEntity(object_id);
            end;
        end;
    end
    
    prepareEntity(id);
end


function damocles_init(id)      -- Sword of Damocles

    setEntityTypeFlag(id, ENTITY_TYPE_GENERIC);
    setEntityCallbackFlag(id, ENTITY_CALLBACK_COLLISION, 1);
    setEntityActivity(id, false);
    
    entity_funcs[id].onActivate = function(object_id, activator_id)
        setEntityActivity(object_id, true);
        entity_funcs[id].rot_speed = ((math.random(20) - 10) / 5) + 1;
        entity_funcs[id].falling = false;
        return ENTITY_TRIGGERING_ACTIVATED;
    end    
    
    entity_funcs[id].onDeactivate = function(object_id, activator_id)
        setEntityActivity(object_id, false);
        entity_funcs[id].rot_speed = 0.0;
        return ENTITY_TRIGGERING_DEACTIVATED;
    end
    
    entity_funcs[id].onLoop = function(object_id)
        rotateEntity(object_id, entity_funcs[object_id].rot_speed);
        
        if(sameRoom(player, object_id)) then
            local dx,dy,dz = getEntityVector(player, object_id);
            if((math.abs(dx) < 1024.0) and (math.abs(dy) < 1024.0) and (entity_funcs[object_id].falling == false)) then
                entity_funcs[object_id].falling = true;
                addTask(
                function()
                    moveEntityToEntity(object_id, player, 32.0, true);
                    if(dropEntity(object_id, frame_time, true)) then
                        playSound(103, object_id);
                        setEntityActivity(object_id, false);
                        entity_funcs[object_id].falling = false;
                        return false;
                    end;
                    return true;
                end);
            end;
        end
    end    
    
    entity_funcs[id].onCollide = function(object_id, activator_id)
        if((entity_funcs[object_id].falling == true) and (getEntityModelID(activator_id) == 0) and (getCharacterParam(activator_id, PARAM_HEALTH) > 0)) then
            setCharacterParam(activator_id, PARAM_HEALTH, 0);
            playSound(SOUND_GEN_DEATH, activator_id);
            playSound(103, object_id);
            addEntityRagdoll(activator_id, RD_TYPE_LARA);
            setEntityActivity(object_id, false);
            setEntityBodyMass(object_id, 1, 15.0);
        end;
    end
    
    entity_funcs[id].onDelete = function(object_id)
        entity_funcs[object_id].rot_speed = nil;
        entity_funcs[object_id].falling = nil;
    end
end


function natla_cabin_TR1_init(id)

    entity_funcs[id].onActivate = function(object_id, activator_id)
        local st = getEntityAnimState(object_id, ANIM_TYPE_BASE);
        local a, f, c = getEntityAnim(object_id, ANIM_TYPE_BASE);
        setEntityAnim(object_id, ANIM_TYPE_BASE, a, c - 1, c - 1);  -- force it to avoid broken state change
        setEntityAnimState(object_id, ANIM_TYPE_BASE, st + 1);
        return ENTITY_TRIGGERING_ACTIVATED;
    end

    entity_funcs[id].onLoop = function(object_id)
        if(getEntityEnability(object_id)) then
            local st = getEntityAnimState(object_id, ANIM_TYPE_BASE);
            if(st == 4) then
                disableEntity(object_id);
                setFlipMap(0x03, 0x1F, TRIGGER_OP_XOR);    --setFlipMap(flip_index, flip_mask, TRIGGER_OP_OR / XOR)
                setFlipState(0x03, 1);                     --setFlipState(flip_index, FLIP_STATE_ON)
                entity_funcs[id].onLoop = nil;
            end;
        else
            entity_funcs[id].onLoop = nil;
        end;
    end
end


----------------------------------------------
-------------------    TR 2  -----------------
----------------------------------------------

function venicebird_init(id)    -- Venice singing birds (TR2)
    randomized_soundsource_init(id);
    entity_funcs[id].chance   = 2;
    entity_funcs[id].sound_id = 316;
end


function drips_init(id)         -- Maria Doria drips (TR2)
    randomized_soundsource_init(id);
    entity_funcs[id].chance   = 5;
    entity_funcs[id].sound_id = 329;
end


function alarm_TR2_init(id)    -- Offshore Rig alarm (TR2)

    gen_soundsource_init(id);
    entity_funcs[id].sound_id = 332;
end


function alarmbell_init(id)    -- Home Sweet Home alarm (TR2)

    gen_soundsource_init(id);
    entity_funcs[id].sound_id = 335;
end


function doorbell_init(id)    -- Lara's Home doorbell (TR2)

    setEntityTypeFlag(id, ENTITY_TYPE_GENERIC);
    setEntityActivity(id, false);
    
    entity_funcs[id].onActivate = function(object_id, activator_id)
        return swapEntityActivity(object_id);
    end
    
    entity_funcs[id].onDeactivate = entity_funcs[id].onActivate;
    
    entity_funcs[id].onLoop = function(object_id)
        if(getEntityDistance(player, object_id) < 4096.0) then
            playSound(334, object_id);
            setEntityActivity(object_id, false);
        end;
    end
end


function heli_rig_TR2_init(id)    -- Helicopter in Offshore Rig (TR2)

    setEntityTypeFlag(id, ENTITY_TYPE_GENERIC);
    setEntityActivity(id, false);
    setEntityVisibility(id, false);
    
    entity_funcs[id].onActivate = function(object_id, activator_id)
        setEntityActivity(object_id, true);
        setEntityVisibility(object_id, 1);
        return ENTITY_TRIGGERING_ACTIVATED;
    end
    
    entity_funcs[id].onLoop = function(object_id)
        if(getEntityLock(object_id)) then
            if(getEntityAnimState(object_id, ANIM_TYPE_BASE) ~= 2) then
                setEntityAnimState(object_id, ANIM_TYPE_BASE, 2);
            else
                local anim, frame, count = getEntityAnim(object_id, ANIM_TYPE_BASE);
                if(frame == count-1) then 
                    disableEntity(object_id) 
                end;
            end
        end;
    end
    
    prepareEntity(id);
end


function heli_TR2_init(id)    -- Helicopter (TR2)

    setEntityTypeFlag(id, ENTITY_TYPE_GENERIC);
    setEntityActivity(id, false);
    setEntityVisibility(id, false);
    
    entity_funcs[id].distance_passed = 0;
    
    entity_funcs[id].onActivate = function(object_id, activator_id)
        if(not getEntityActivity(object_id)) then
            setEntityActivity(object_id, true);
            setEntityVisibility(id, 1);
            playSound(297, object_id);
        end;
        return ENTITY_TRIGGERING_ACTIVATED;
    end
    
    entity_funcs[id].onDeactivate = function(object_id, activator_id)
        setEntityActivity(object_id, false);
        return ENTITY_TRIGGERING_DEACTIVATED;
    end
    
    entity_funcs[id].onLoop = function(object_id)
        local dy = 40.0 * 60.0 * frame_time;
        entity_funcs[object_id].distance_passed = entity_funcs[object_id].distance_passed + dy;
        moveEntityLocal(object_id, 0.0, dy, 0.0);
        if(entity_funcs[object_id].distance_passed > 30720) then
            stopSound(297, object_id);
            disableEntity(object_id);
        end;
    end
    
    entity_funcs[id].onDelete = function(object_id)
        entity_funcs[object_id].distance_passed = nil;
    end
    
    prepareEntity(id);
end


----------------------------------------------
-------------------    TR 3  -----------------
----------------------------------------------

function crystal_TR3_init(id)   -- "Savegame" crystal (TR3 version)

    setEntityTypeFlag(id, ENTITY_TYPE_GENERIC);
    setEntityActivity(id, true);
    
    entity_funcs[id].onLoop = function(object_id)
        if(getEntityDistance(player, object_id) < 512.0) then
            playSound(SOUND_MEDIPACK);
            changeCharacterParam(player, PARAM_HEALTH, 200);
            disableEntity(object_id);
        end;
    end
end


function cleaner_init(id)      -- Thames Wharf machine (aka cleaner)

    disableEntity(id);
    
    setEntityTypeFlag(id, ENTITY_TYPE_GENERIC);
    setEntityCallbackFlag(id, ENTITY_CALLBACK_COLLISION, 1);
    
    -- Initialize specific cleaner parameters.
    
    entity_funcs[id].rotating = 0;                  -- Specifies if cleaner is in rotating phase or not.
    entity_funcs[id].distance_traveled = 1024.0;    -- Each 1024 units, cleaner checks floor state.
                                                    -- Must be 1024 by default to process initial floor state correctly!
    entity_funcs[id].current_angle = 0.0;           -- Current cleaner rotating angle.
    entity_funcs[id].target_angle  = 0.0;           -- Desired angle value, after which cleaner stops rotating.
    
    entity_funcs[id].turn_rot_speed  = 60 * 2.0;    -- Rotation speed for free left-turn.
    entity_funcs[id].stuck_rot_speed = 60 * 2.0;    -- Rotation speed for face-wall right turn.
    entity_funcs[id].move_speed      = 60 * 32.0;   -- Ordinary movement speed.
    
    entity_funcs[id].loop_detector = {};            -- Loop detector struct needed to prevent cleaner stuck at flat floor.
    
    entity_funcs[id].loop_detector[1] = {x = 0.0, y = 0.0};
    entity_funcs[id].loop_detector[2] = {x = 0.0, y = 0.0};
    entity_funcs[id].loop_detector[3] = {x = 0.0, y = 0.0};
    entity_funcs[id].loop_detector[4] = {x = 0.0, y = 0.0};
    
    entity_funcs[id].move_count = 1;                -- Needed to fill loop detector struct each 4 moves.
    entity_funcs[id].loop_count = 0;                -- If loop count reaches a value of 4, cleaner will turn right, not left.
    
    entity_funcs[id].dead = 0;                      -- Needed to process fuse box collision correctly.
    
    
    entity_funcs[id].onActivate = function(object_id, activator_id)
        if(not getEntityActivity(object_id)) then 
            enableEntity(object_id) 
        end;
        return ENTITY_TRIGGERING_ACTIVATED;
    end    
    
    entity_funcs[id].onDeactivate = function(object_id, activator_id)
        playSound(131, object_id);
        stopSound(191, object_id);
        setEntityActivity(object_id, false);
        return ENTITY_TRIGGERING_DEACTIVATED;
    end
    
    entity_funcs[id].onLoop = function(object_id)
        local px,py,pz,ax = getEntityPos(object_id);
        playSound(191, object_id);
        
        if(entity_funcs[object_id].rotating == 0) then
            if(entity_funcs[object_id].distance_traveled >= 1024.0) then
                if(similarSector(object_id, -1024.0, 0.0, 0.0, false)) then  -- Rotate left
                    entity_funcs[object_id].rotating = 1;
                    entity_funcs[object_id].current_angle = 0.0;    -- DO NOT CHANGE!!!
                    entity_funcs[object_id].target_angle = 90.0;    -- Change if you want different "free" turning angle.
                    px = (math.floor((px / 512.0) + 0.5)) * 512.0;  -- round position
                    py = (math.floor((py / 512.0) + 0.5)) * 512.0;  -- round position
                    setEntityPos(object_id, px,py,pz,ax,ay,az);
                    entity_funcs[object_id].distance_traveled = 0.0;
                elseif((entity_funcs[object_id].loop_count > 3) or
                   (not similarSector(object_id, 0.0, 520.0, 0.0, false))) then   -- Rotate right
                    entity_funcs[object_id].loop_count = 0;
                    entity_funcs[object_id].rotating = 2;
                    entity_funcs[object_id].current_angle = 0.0;    -- DO NOT CHANGE!!!
                    entity_funcs[object_id].target_angle = 90.0;    -- Change if you want different "stuck" turning angle.
                    px = (math.floor((px / 512.0) + 0.5)) * 512.0;  -- round position
                    py = (math.floor((py / 512.0) + 0.5)) * 512.0;  -- round position
                    setEntityPos(object_id, px,py,pz,ax,ay,az);
                end;
                
                -- Loop detector algorithm: each turn cleaner stores its current position in an array of 4.
                -- If current array unit is equal to current position, it will increase loop counter, otherwise
                -- loop counter is reset back to zero. When loop counter reaches 4, robot is forced to turn right,
                -- not left. This prevents robot from being stuck at 2x2 flat floor section.
                
                if(entity_funcs[object_id].rotating == 0) then
                    entity_funcs[object_id].distance_traveled = 0.0;
                    local dy = entity_funcs[object_id].move_speed * frame_time;
                    moveEntityLocal(object_id, 0.0, dy, 0.0);  -- Move forward...
                    entity_funcs[object_id].distance_traveled = entity_funcs[object_id].distance_traveled + dy;
                end;
                
                if((entity_funcs[object_id].loop_detector[(entity_funcs[object_id].move_count)].x == px) and
                   (entity_funcs[object_id].loop_detector[(entity_funcs[object_id].move_count)].y == py)) then
                    entity_funcs[object_id].loop_count = entity_funcs[object_id].loop_count + 1;
                else
                    entity_funcs[object_id].loop_count = 0;
                end;
                
                entity_funcs[object_id].loop_detector[(entity_funcs[object_id].move_count)].x = px;
                entity_funcs[object_id].loop_detector[(entity_funcs[object_id].move_count)].y = py;
                 
                entity_funcs[object_id].move_count = entity_funcs[object_id].move_count + 1;
                if(entity_funcs[object_id].move_count > 4) then entity_funcs[object_id].move_count = 1 end;
            else
                local dy = entity_funcs[object_id].move_speed * frame_time;
                moveEntityLocal(object_id, 0.0, dy, 0.0);  -- Move forward...
                entity_funcs[object_id].distance_traveled = entity_funcs[object_id].distance_traveled + dy;
            end;
        else            
            if(entity_funcs[object_id].rotating == 1) then
                local dza = entity_funcs[object_id].turn_rot_speed * frame_time;
                entity_funcs[object_id].current_angle = entity_funcs[object_id].current_angle + dza;
                rotateEntity(object_id, dza);
            elseif(entity_funcs[object_id].rotating == 2) then
                local dza = entity_funcs[object_id].stuck_rot_speed * frame_time;
                entity_funcs[object_id].current_angle = entity_funcs[object_id].current_angle + dza;
                rotateEntity(object_id, -dza); -- another direction!
            end;
            
            if(entity_funcs[object_id].current_angle > entity_funcs[object_id].target_angle) then
                ax = (math.floor((ax / 90.0) + 0.5)) * 90.0;        -- round angle
                setEntityPos(object_id, px,py,pz,ax,ay,az);
                entity_funcs[object_id].rotating = 0;
                entity_funcs[object_id].current_angle = 0.0;
                entity_funcs[object_id].target_angle = 0.0;
                entity_funcs[object_id].rotating = 0;
            end;
        end;
    end;
    
    entity_funcs[id].onCollide = function(object_id, activator_id)
        if(getEntityActivity(object_id)) then
            a_model = getEntityModelID(activator_id);
            if((a_model == 0) and (getCharacterParam(activator_id, PARAM_HEALTH) > 0)) then  -- Lara
                setEntityActivity(object_id, false);
                addEntityRagdoll(activator_id, RD_TYPE_LARA);
                setCharacterParam(activator_id, PARAM_HEALTH, 0);
                stopSound(191, object_id);
                playSound(SOUND_GEN_DEATH, activator_id);
                playSound(127, activator_id);
                playSound(131, object_id);
            elseif(a_model == 354) then -- Fuse box - DOESN'T WORK FOR A MOMENT! (no collision callback from fusebox).
                if(not entity_funcs[object_id].dead) then
                    setEntityTypeFlag(object_id, ENTITY_TYPE_HEAVYTRIGGER_ACTIVATOR, 1);
                    stopSound(191, object_id);
                    entity_funcs[object_id].dead = 1;
                elseif(entity_funcs[object_id].dead) then
                    setEntityActivity(object_id, false);
                    playSound(131, object_id);
                    setEntityCallbackFlag(object_id, ENTITY_CALLBACK_COLLISION, 0);
                end;
            end;
        end;
    end
    
    entity_funcs[id].onDelete = function(object_id)
        entity_funcs[object_id].rotating            = nil;
        entity_funcs[object_id].distance_traveled   = nil;
        entity_funcs[object_id].current_angle       = nil;
        entity_funcs[object_id].target_angle        = nil;
        entity_funcs[object_id].turn_rot_speed      = nil;
        entity_funcs[object_id].stuck_rot_speed     = nil;
        entity_funcs[object_id].move_speed          = nil;
        entity_funcs[object_id].move_count          = nil;
        entity_funcs[object_id].loop_count          = nil;
        entity_funcs[object_id].dead                = nil;
        
        for k,v in pairs(entity_funcs[object_id].loop_detector) do
            entity_funcs[object_id].loop_detector[k].x = nil;
            entity_funcs[object_id].loop_detector[k].y = nil;
            entity_funcs[object_id].loop_detector[k]   = nil;
        end;
        
        entity_funcs[object_id].loop_detector       = nil;
    end
end




----------------------------------------------
-------------------    TR 4  -----------------
----------------------------------------------

function slicerdicer_init(id)      -- Slicer-dicer (TR4)

    disableEntity(id);
    setEntityTypeFlag(id, ENTITY_TYPE_GENERIC);
    setEntityCallbackFlag(id, ENTITY_CALLBACK_COLLISION, 1);
    
    entity_funcs[id].current_angle =  0.0;
    entity_funcs[id].speed         = -0.5 * 60.0;  -- Full turn in 14 seconds, clockwise.
    entity_funcs[id].radius        =  math.abs(4096.0 * entity_funcs[id].speed / 60);
    
    moveEntityLocal(id, 512.0, 0.0, 0.0);  -- Fix position
    
    entity_funcs[id].onActivate = function(object_id, activator_id)
        if(not getEntityActivity(object_id)) then 
            enableEntity(object_id);
            return ENTITY_TRIGGERING_ACTIVATED;
        else 
            disableEntity(object_id)
            return ENTITY_TRIGGERING_DEACTIVATED;
        end;
    end;
    
    entity_funcs[id].onDeactivate = entity_funcs[id].onActivate;
    
    entity_funcs[id].onLoop = function(object_id)
        if(tickEntity(object_id) == TICK_STOPPED) then
            setEntityActivity(object_id, false);
        end;
        
        entity_funcs[object_id].current_angle = entity_funcs[object_id].current_angle + entity_funcs[object_id].speed * frame_time;
        
        if(entity_funcs[object_id].current_angle < 0.0) then
            entity_funcs[object_id].current_angle = 360.0 + entity_funcs[object_id].current_angle;
        elseif(entity_funcs[object_id].current_angle > 360.0) then
            entity_funcs[object_id].current_angle = entity_funcs[object_id].current_angle - 360.0;
        end;
        
        moveEntityLocal(object_id, 0.0, math.cos(math.rad(entity_funcs[object_id].current_angle)) * entity_funcs[object_id].radius, -math.sin(math.rad(entity_funcs[object_id].current_angle)) * entity_funcs[object_id].radius);
        
    end
    
    entity_funcs[id].onCollide = function(object_id, activator_id)
        if(getEntityActivity(object_id)) then changeCharacterParam(activator_id, PARAM_HEALTH, -35.0) end;
    end
    
    entity_funcs[id].onDelete = function(object_id)
        entity_funcs[object_id].current_angle = nil;
        entity_funcs[object_id].speed         = nil;
        entity_funcs[object_id].radius        = nil;
    end
    
    prepareEntity(id);
end


function plough_init(id)     -- Plough (TR4)

    setEntityTypeFlag(id, ENTITY_TYPE_GENERIC);
    setEntityCallbackFlag(id, ENTITY_CALLBACK_COLLISION, 1);
    setEntityActivity(id, false);
    
    entity_funcs[id].onActivate = function(object_id, activator_id)
        setEntityActivity(object_id, true);
        return ENTITY_TRIGGERING_ACTIVATED;
    end    
    
    entity_funcs[id].onDeactivate = function(object_id, activator_id)
        setEntityActivity(object_id, false);
        return ENTITY_TRIGGERING_DEACTIVATED;
    end
    
    entity_funcs[id].onLoop = function(object_id)
        if(tickEntity(object_id) == TICK_STOPPED) then setEntityActivity(object_id, 0) end;
    end
    
    entity_funcs[id].onCollide = function(object_id, activator_id)
        if(getEntityActivity(object_id)) then
            changeCharacterParam(activator_id, PARAM_HEALTH, -50 * 60.0 * frame_time);
        end;
    end
    
    prepareEntity(id);
end


----------------------------------------------
-------------------    TR 5  -----------------
----------------------------------------------
