#include <avr/pgmspace.h>

#include "Configuration.h"
#ifdef ENABLE_ULTILCD2
#include "Marlin.h"
// #include "cardreader.h"//This code uses the card.longFilename as buffer to store data, to save memory.
#include "temperature.h"
#include "machinesettings.h"
#include "UltiLCD2.h"
#include "UltiLCD2_hi_lib.h"
#include "UltiLCD2_menu_print.h"
#include "UltiLCD2_menu_material.h"
#include "UltiLCD2_menu_maintenance.h"
#include "UltiLCD2_menu_utils.h"
#include "preferences.h"
#include "commandbuffer.h"
#if (EXTRUDERS > 1)
#include "UltiLCD2_menu_dual.h"
#endif

struct materialSettings material[EXTRUDERS];
static unsigned long preheat_end_time;

void doCooldown();//TODO
static void lcd_menu_change_material_remove();
static void lcd_menu_change_material_remove_wait_user();
static void lcd_menu_change_material_remove_wait_user_ready();
static void lcd_menu_change_material_insert_wait_user();
static void lcd_menu_change_material_insert_wait_user_ready();
static void lcd_menu_change_material_insert_forward();
static void lcd_menu_change_material_insert();
static void lcd_menu_change_material_select_material();
static void lcd_menu_material_selected();
static void lcd_menu_material_settings();
static void lcd_menu_material_temperature_settings();
static void lcd_menu_material_settings_store();

static void cancelMaterialInsert()
{
    quickStop();
    //Set E motor power to default.
#if EXTRUDERS > 1 && defined(MOTOR_CURRENT_PWM_E_PIN) && MOTOR_CURRENT_PWM_E_PIN > -1
    digipot_current(2, menu_extruder ? motor_current_e2 : motor_current_setting[2]);
#else
    digipot_current(2, motor_current_setting[2]);
#endif
    set_extrude_min_temp(EXTRUDE_MINTEMP);
    menu.return_to_previous(false);
}

void lcd_material_change_init(bool printing)
{
    if (!printing)
    {
        minProgress = 0;
#if EXTRUDERS > 1
        if ((menu_extruder == 0) || (menu_extruder == active_extruder))
#endif
        {
            // move head to front
            CommandBuffer::homeHead();
            cmd_synchronize();
            CommandBuffer::move2front();
        }
        menu.add_menu(menu_t(lcd_menu_material_main_return));
    }
    preheat_end_time = millis() + (unsigned long)material[menu_extruder].change_preheat_wait_time * 1000L;
}

void lcd_menu_material_main_return()
{
    for(uint8_t n=0; n<EXTRUDERS; ++n)
    {
        setTargetHotend(0, n);
    }
    fanSpeed = 0;
#if EXTRUDERS > 1
    if ((menu_extruder == 0) || (menu_extruder == active_extruder))
#endif
    {
        cmd_synchronize();
        CommandBuffer::homeHead();
    }
    enquecommand_P(PSTR("M84 X Y E"));
    menu.return_to_previous(false);
}

void lcd_menu_material_main()
{
    lcd_tripple_menu(PSTR("CHANGE"), PSTR("SETTINGS"), PSTR("RETURN"));

    if (lcd_lib_button_pressed)
    {
        if (IS_SELECTED_MAIN(0) && !commands_queued())
        {
            lcd_material_change_init(false);
            menu.add_menu(menu_t(lcd_menu_change_material_preheat));
        }
        else if (IS_SELECTED_MAIN(1))
            menu.add_menu(menu_t(lcd_menu_material_select, SCROLL_MENU_ITEM_POS(0)));
        else if (IS_SELECTED_MAIN(2))
            menu.return_to_previous();
    }

    lcd_lib_update_screen();
}

void lcd_menu_change_material_preheat()
{
    last_user_interaction = millis();
#if (EXTRUDERS > 1)
  #ifdef USE_CHANGE_TEMPERATURE
    setTargetHotend(material[menu_extruder].change_temperature, menu_extruder);
  #else
    setTargetHotend(material[menu_extruder].temperature[0], menu_extruder);
  #endif
    int16_t temp = degHotend(menu_extruder) - 20;
    int16_t target = degTargetHotend(menu_extruder) - 20;
#else
  #ifdef USE_CHANGE_TEMPERATURE
    setTargetHotend(material[active_extruder].change_temperature, active_extruder);
  #else
    setTargetHotend(material[active_extruder].temperature[0], active_extruder);
  #endif
    int16_t temp = degHotend(active_extruder) - 20;
    int16_t target = degTargetHotend(active_extruder) - 20;
#endif
    if (temp < 0) temp = 0;

    // draw menu
#if (EXTRUDERS > 1)
    char buffer[8] = {0};
#endif
    uint8_t progress = uint8_t(temp * 125 / target);
    if (progress < minProgress)
        progress = minProgress;
    else
        minProgress = progress;

    lcd_info_screen(NULL, cancelMaterialInsert);
    if (temp < target + 10)
        lcd_lib_draw_stringP(3, 10, PSTR("Heating nozzle"));
    else
        lcd_lib_draw_stringP(3, 10, PSTR("Cooling nozzle"));

#if EXTRUDERS > 1
    strcpy_P(buffer, PSTR("("));
    int_to_string(menu_extruder+1, buffer+1, PSTR(")"));
    lcd_lib_draw_string(3+(15*LCD_CHAR_SPACING), 10, buffer);
#endif
    lcd_lib_draw_stringP(3, 20, PSTR("for material removal"));

    lcd_progressbar(progress);


    // check target temp and waiting time
    if (temp > target - 5 && temp < target + 5)
    {
        if (preheat_end_time < last_user_interaction)
        {
            quickStop();
            set_extrude_min_temp(0);
            current_position[E_AXIS] = 0.0f;
            plan_set_e_position(current_position[E_AXIS], menu_extruder, true);

            //float old_max_feedrate_e = max_feedrate[E_AXIS];
            float old_retract_acceleration = retract_acceleration;
            float old_max_e_jerk = max_e_jerk;

            //max_feedrate[E_AXIS] = float(FILAMENT_FAST_STEPS) / e_steps_per_unit(menu_extruder);
            retract_acceleration = float(FILAMENT_LONG_ACCELERATION_STEPS) / e_steps_per_unit(menu_extruder);
            max_e_jerk = FILAMENT_LONG_MOVE_JERK;

            current_position[E_AXIS] -= 1.0 / volume_to_filament_length[menu_extruder];
            plan_buffer_line(current_position[X_AXIS], current_position[Y_AXIS], current_position[Z_AXIS], current_position[E_AXIS], max_feedrate[E_AXIS], menu_extruder);
            current_position[E_AXIS] -= FILAMENT_REVERSAL_LENGTH / volume_to_filament_length[menu_extruder];
            plan_buffer_line(current_position[X_AXIS], current_position[Y_AXIS], current_position[Z_AXIS], current_position[E_AXIS], max_feedrate[E_AXIS], menu_extruder);

            //max_feedrate[E_AXIS] = old_max_feedrate_e;
            retract_acceleration = old_retract_acceleration;
            max_e_jerk = old_max_e_jerk;

            menu.replace_menu(menu_t(lcd_menu_change_material_remove), false);
            return;
        }
    }
    else
    {
#ifdef USE_CHANGE_TEMPERATURE
        preheat_end_time = last_user_interaction + (unsigned long)material[menu_extruder].change_preheat_wait_time * 1000UL;
#else
        preheat_end_time = last_user_interaction;
#endif
    }

    lcd_lib_update_screen();
}

static void lcd_menu_change_material_remove()
{
    last_user_interaction = millis();

    if (!blocks_queued())
    {
        menu.replace_menu(menu_t(lcd_menu_change_material_remove_wait_user, MAIN_MENU_ITEM_POS(0)));
        //Disable the extruder motor so you can pull out the remaining filament.
        disable_e0();
        disable_e1();
        disable_e2();
    #if EXTRUDERS > 1
        last_extruder = 0xFF;
    #endif
        return;
    }

    lcd_info_screen(NULL, cancelMaterialInsert);
#if EXTRUDERS > 1
    lcd_lib_draw_stringP(3, 10, PSTR("Extruder"));
    char buffer[8] = {0};
    strcpy_P(buffer, PSTR("("));
    int_to_string(menu_extruder+1, buffer+1, PSTR(")"));
    lcd_lib_draw_string(3+(9*LCD_CHAR_SPACING), 10, buffer);
#endif
    lcd_lib_draw_stringP(3, 20, PSTR("Reversing material"));

    long pos = -st_get_position(E_AXIS);
    long targetPos = lround(FILAMENT_REVERSAL_LENGTH * e_steps_per_unit(active_extruder));
    uint8_t progress = (pos * 125 / targetPos);
    lcd_progressbar(progress);

    lcd_lib_update_screen();
}

static void lcd_menu_change_material_remove_wait_user_ready()
{
    st_synchronize();
    // plan_set_e_position(0.0);
    // current_position[E_AXIS] = 0.0;
    menu.replace_menu(menu_t(lcd_menu_change_material_select_material, SCROLL_MENU_ITEM_POS(0)));
    check_preheat(menu_extruder);
}

static void lcd_menu_change_material_remove_wait_user()
{
    LED_GLOW
    lcd_question_screen(NULL, lcd_menu_change_material_remove_wait_user_ready, PSTR("READY"), NULL, cancelMaterialInsert, PSTR("CANCEL"));
#if EXTRUDERS > 1
    lcd_lib_draw_stringP(3, 10, PSTR("Extruder"));
    char buffer[8] = {0};
    strcpy_P(buffer, PSTR("("));
    int_to_string(menu_extruder+1, buffer+1, PSTR(")"));
    lcd_lib_draw_string(3+(9*LCD_CHAR_SPACING), 10, buffer);
    lcd_lib_draw_stringP(3, 20, PSTR("Remove material"));
#else
    lcd_lib_draw_string_centerP(20, PSTR("Remove material"));
#endif
    lcd_lib_update_screen();
}

void lcd_menu_insert_material_preheat()
{
    last_user_interaction = millis();
    setTargetHotend(material[menu_extruder].temperature[0], menu_extruder);
    int16_t temp = degHotend(menu_extruder) - 20;
    int16_t target = degTargetHotend(menu_extruder) - 20 - 10;
    if (temp < 0) temp = 0;
    if (temp > target && temp < target + 20 && (card.pause() || !commands_queued()))
    {
        set_extrude_min_temp(0);
        menu.replace_menu(menu_t(lcd_menu_change_material_insert_wait_user, MAIN_MENU_ITEM_POS(0)));
        temp = target;
    }

    uint8_t progress = uint8_t(temp * 125 / target);
    if (progress < minProgress)
        progress = minProgress;
    else
        minProgress = progress;

    lcd_info_screen(NULL, cancelMaterialInsert);
#if EXTRUDERS > 1
    if (temp < target + 10)
        lcd_lib_draw_stringP(3, 10, PSTR("Heating nozzle"));
    else
        lcd_lib_draw_stringP(3, 10, PSTR("Cooling nozzle"));

    char buffer[8] = {0};
    strcpy_P(buffer, PSTR("("));
    int_to_string(menu_extruder+1, buffer+1, PSTR(")"));
    lcd_lib_draw_string(3+(15*LCD_CHAR_SPACING), 10, buffer);
    lcd_lib_draw_stringP(3, 20, PSTR("for insertion"));
#else
    if (temp < target + 10)
        lcd_lib_draw_stringP(3, 10, PSTR("Heating nozzle for"));
    else
        lcd_lib_draw_stringP(3, 10, PSTR("Cooling nozzle for"));
    lcd_lib_draw_stringP(3, 20, PSTR("material insertion"));
#endif

    lcd_progressbar(progress);

    lcd_lib_update_screen();
}

static void lcd_menu_change_material_insert_wait_user()
{
    LED_GLOW

    if (target_temperature[menu_extruder] && (printing_state == PRINT_STATE_NORMAL))
    {
        if (movesplanned() < 2)
        {
            current_position[E_AXIS] += 0.5 / volume_to_filament_length[menu_extruder];
            plan_buffer_line(current_position[X_AXIS], current_position[Y_AXIS], current_position[Z_AXIS], current_position[E_AXIS], FILAMENT_INSERT_SPEED, menu_extruder);
        }
    }
    else
    {
        cancelMaterialInsert();
        return;
    }

    lcd_question_screen(NULL, lcd_menu_change_material_insert_wait_user_ready, PSTR("READY"), NULL, cancelMaterialInsert, PSTR("CANCEL"));
#if EXTRUDERS > 1
    lcd_lib_draw_stringP(3, 10, PSTR("Insert new material"));
    lcd_lib_draw_stringP(3, 20, PSTR("for extruder"));
    char buffer[8] = {0};
    strcpy_P(buffer, PSTR("("));
    int_to_string(menu_extruder+1, buffer+1, PSTR(")"));
    lcd_lib_draw_string(3+(13*LCD_CHAR_SPACING), 20, buffer);
    lcd_lib_draw_stringP(3, 30, PSTR("from the backside of"));
    lcd_lib_draw_stringP(3, 40, PSTR("your machine"));
#else
    lcd_lib_draw_string_centerP(10, PSTR("Insert new material"));
    lcd_lib_draw_string_centerP(20, PSTR("from the backside of"));
    lcd_lib_draw_string_centerP(30, PSTR("your machine,"));
    lcd_lib_draw_string_centerP(40, PSTR("above the arrow."));
#endif
    lcd_lib_update_screen();

}

static void lcd_menu_change_material_insert_wait_user_ready()
{
    // heat up nozzle (if necessary)
    if (!check_preheat(menu_extruder))
    {
        return;
    }

    //Override the max feedrate and acceleration values to get a better insert speed and speedup/slowdown
    //float old_max_feedrate_e = max_feedrate[E_AXIS];
    float old_retract_acceleration = retract_acceleration;
    float old_max_e_jerk = max_e_jerk;

    //max_feedrate[E_AXIS] = float(FILAMENT_FAST_STEPS) / e_steps_per_unit(menu_extruder);
    retract_acceleration = float(FILAMENT_LONG_ACCELERATION_STEPS) / e_steps_per_unit(menu_extruder);
    max_e_jerk = FILAMENT_LONG_MOVE_JERK;

    quickStop();
    current_position[E_AXIS] = 0.0f;
    plan_set_e_position(current_position[E_AXIS], menu_extruder, true);

    current_position[E_AXIS] += FILAMENT_FORWARD_LENGTH / volume_to_filament_length[menu_extruder];
    plan_buffer_line(current_position[X_AXIS], current_position[Y_AXIS], current_position[Z_AXIS], current_position[E_AXIS], max_feedrate[E_AXIS], menu_extruder);

    //Put back original values.
    //max_feedrate[E_AXIS] = old_max_feedrate_e;
    retract_acceleration = old_retract_acceleration;
    max_e_jerk = old_max_e_jerk;

    menu.replace_menu(menu_t(lcd_menu_change_material_insert_forward));
}

static void lcd_menu_change_material_insert_forward()
{
    last_user_interaction = millis();
    if (!blocks_queued())
    {
        lcd_lib_keyclick();
        // led_glow_dir = led_glow = 0;

        //Set the E motor power lower to we skip instead of grind.
#if EXTRUDERS > 1 && defined(MOTOR_CURRENT_PWM_E_PIN) && MOTOR_CURRENT_PWM_E_PIN > -1
        digipot_current(2, menu_extruder ? (motor_current_e2*2/3) : (motor_current_setting[2]*2/3));
#else
        digipot_current(2, motor_current_setting[2]*2/3);
#endif
        menu.replace_menu(menu_t(lcd_menu_change_material_insert, MAIN_MENU_ITEM_POS(0)));
        return;
    }

    lcd_info_screen(NULL, cancelMaterialInsert);

#if EXTRUDERS > 1
    lcd_lib_draw_stringP(3, 10, PSTR("Extruder"));
    char buffer[8] = {0};
    strcpy_P(buffer, PSTR("("));
    int_to_string(menu_extruder+1, buffer+1, PSTR(")"));
    lcd_lib_draw_string(3+(9*LCD_CHAR_SPACING), 10, buffer);
#endif
    lcd_lib_draw_stringP(3, 20, PSTR("Forwarding material"));

    long pos = st_get_position(E_AXIS);
    long targetPos = lround(FILAMENT_FORWARD_LENGTH*e_steps_per_unit(active_extruder));
    uint8_t progress = (pos * 125 / targetPos);
    lcd_progressbar(progress);

    lcd_lib_update_screen();
}

static void materialInsertReady()
{
    //Set E motor power to default.
    quickStop();
#if EXTRUDERS > 1 && defined(MOTOR_CURRENT_PWM_E_PIN) && MOTOR_CURRENT_PWM_E_PIN > -1
    digipot_current(2, menu_extruder ? motor_current_e2 : motor_current_setting[2]);
#else
    digipot_current(2, motor_current_setting[2]);
#endif
    lcd_remove_menu();

    // retract material
    current_position[E_AXIS] = 0.0f;
    plan_set_e_position(current_position[E_AXIS], menu_extruder, true);
    if (EXTRUDER_RETRACTED(menu_extruder))
    {
        current_position[E_AXIS] -= retract_recover_length[menu_extruder];
    }
    else
    {
        current_position[E_AXIS] -= end_of_print_retraction / volume_to_filament_length[menu_extruder];
    }
    plan_buffer_line(current_position[X_AXIS], current_position[Y_AXIS], current_position[Z_AXIS], current_position[E_AXIS], retract_feedrate/60, menu_extruder);

    if (!card.sdprinting())
    {
        // cool down nozzle
        for(uint8_t n=0; n<EXTRUDERS; n++)
        {
            setTargetHotend(0, n);
        }
    }
}

static void lcd_menu_change_material_insert()
{
    if (target_temperature[menu_extruder])
    {
        LED_GLOW

        lcd_question_screen(lcd_change_to_previous_menu, materialInsertReady, PSTR("READY"), NULL, cancelMaterialInsert, PSTR("CANCEL"));

#if EXTRUDERS > 1
        lcd_lib_draw_stringP(3, 20, PSTR("Wait till material"));
        lcd_lib_draw_stringP(3, 30, PSTR("comes out nozzle"));
        char buffer[8] = {0};
        strcpy_P(buffer, PSTR("("));
        int_to_string(menu_extruder+1, buffer+1, PSTR(")"));
        lcd_lib_draw_string(3+(17*LCD_CHAR_SPACING), 30, buffer);
#else
        lcd_lib_draw_string_centerP(20, PSTR("Wait till material"));
        lcd_lib_draw_string_centerP(30, PSTR("comes out the nozzle"));
#endif

        if (movesplanned() < 2)
        {
            current_position[E_AXIS] += 0.5 / volume_to_filament_length[menu_extruder];
            plan_buffer_line(current_position[X_AXIS], current_position[Y_AXIS], current_position[Z_AXIS], current_position[E_AXIS], FILAMENT_INSERT_EXTRUDE_SPEED, menu_extruder);
        }
        lcd_lib_update_screen();
    }
    else
    {
        materialInsertReady();
        menu.replace_menu(menu_t(lcd_menu_change_material_select_material));
    }
}

static void lcd_menu_change_material_select_material_callback(uint8_t nr, uint8_t offsetY, uint8_t flags)
{
    char buffer[10];
    eeprom_read_block(buffer, EEPROM_MATERIAL_NAME_OFFSET(nr), MATERIAL_NAME_SIZE);
    buffer[MATERIAL_NAME_SIZE] = '\0';
    lcd_draw_scroll_entry(offsetY, buffer, flags);
}

static void lcd_menu_change_material_select_material_details_callback(uint8_t nr)
{
    char buffer[32] = {0};
    char* c = buffer;

    if (led_glow_dir)
    {
        c = float_to_string2(eeprom_read_float(EEPROM_MATERIAL_DIAMETER_OFFSET(nr)), c, PSTR("mm"));
        while(c < buffer + 10) *c++ = ' ';
        strcpy_P(c, PSTR("Flow:"));
        c += 5;
        c = int_to_string(eeprom_read_word(EEPROM_MATERIAL_FLOW_OFFSET(nr)), c, PSTR("%"));
    }else{
        c = int_to_string(eeprom_read_word(EEPROM_MATERIAL_TEMPERATURE_OFFSET(nr)), c, PSTR("C"));
#if TEMP_SENSOR_BED != 0
        *c++ = ' ';
        c = int_to_string(eeprom_read_word(EEPROM_MATERIAL_BED_TEMPERATURE_OFFSET(nr)), c, PSTR("C"));
#endif
        while(c < buffer + 10) *c++ = ' ';
        strcpy_P(c, PSTR("Fan: "));
        c += 5;
        c = int_to_string(eeprom_read_byte(EEPROM_MATERIAL_FAN_SPEED_OFFSET(nr)), c, PSTR("%"));
    }
    lcd_lib_draw_string_left(BOTTOM_MENU_YPOS, buffer);
}

static void lcd_menu_change_material_select_material()
{
    uint8_t count = eeprom_read_byte(EEPROM_MATERIAL_COUNT_OFFSET());

    lcd_scroll_menu(PSTR("MATERIAL"), count, lcd_menu_change_material_select_material_callback, lcd_menu_change_material_select_material_details_callback);
    if (lcd_lib_button_pressed)
    {
        lcd_material_set_material(SELECTED_SCROLL_MENU_ITEM(), menu_extruder);

        menu.replace_menu(menu_t(lcd_menu_insert_material_preheat, MAIN_MENU_ITEM_POS(0)));
    }
    lcd_lib_update_screen();
}

static void lcd_menu_material_export_done()
{
    lcd_lib_encoder_pos = MAIN_MENU_ITEM_POS(0);
    lcd_info_screen(NULL, lcd_change_to_previous_menu, PSTR("Ok"));
    lcd_lib_draw_string_centerP(20, PSTR("Saved materials"));
    lcd_lib_draw_string_centerP(30, PSTR("to the SD card"));
    lcd_lib_draw_string_centerP(40, PSTR("in MATERIAL.TXT"));
    lcd_lib_update_screen();
}

static void lcd_menu_material_export()
{
    if (!card.sdInserted())
    {
        LED_GLOW
        lcd_lib_encoder_pos = MAIN_MENU_ITEM_POS(0);
        lcd_info_screen(NULL, lcd_change_to_previous_menu);
        lcd_lib_draw_string_centerP(15, PSTR("No SD-CARD!"));
        lcd_lib_draw_string_centerP(25, PSTR("Please insert card"));
        lcd_lib_update_screen();
        card.release();
        return;
    }
    if (!card.isOk())
    {
        lcd_info_screen(NULL, lcd_change_to_previous_menu);
        lcd_lib_draw_string_centerP(16, PSTR("Reading card..."));
        lcd_lib_update_screen();
        card.initsd();
        return;
    }

    card.setroot();
    card.openFile("MATERIAL.TXT", false);
    uint8_t count = eeprom_read_byte(EEPROM_MATERIAL_COUNT_OFFSET());
    for(uint8_t n=0; n<count; ++n)
    {
        char buffer[32] = {0};
        strcpy_P(buffer, PSTR("[material]\n"));
        card.write_string(buffer);

        strcpy_P(buffer, PSTR("name="));
        char* ptr = buffer + strlen(buffer);
        eeprom_read_block(ptr, EEPROM_MATERIAL_NAME_OFFSET(n), MATERIAL_NAME_SIZE);
        ptr[MATERIAL_NAME_SIZE] = '\0';
        strcat_P(buffer, PSTR("\n"));
        card.write_string(buffer);

        strcpy_P(buffer, PSTR("temperature="));
        ptr = buffer + strlen(buffer);
        int_to_string(eeprom_read_word(EEPROM_MATERIAL_TEMPERATURE_OFFSET(n)), ptr, PSTR("\n"));
        card.write_string(buffer);

        for(uint8_t nozzle=0; nozzle<MATERIAL_TEMPERATURE_COUNT; ++nozzle)
        {
            strcpy_P(buffer, PSTR("temperature_"));
            ptr = float_to_string2(nozzleIndexToNozzleSize(nozzle), buffer + strlen(buffer), PSTR("="));
            int_to_string(eeprom_read_word(EEPROM_MATERIAL_EXTRA_TEMPERATURE_OFFSET(n, nozzle)), ptr, PSTR("\n"));
            card.write_string(buffer);
        }

#if TEMP_SENSOR_BED != 0
        strcpy_P(buffer, PSTR("bed_temperature="));
        ptr = buffer + strlen(buffer);
        int_to_string(eeprom_read_word(EEPROM_MATERIAL_BED_TEMPERATURE_OFFSET(n)), ptr, PSTR("\n"));
        card.write_string(buffer);
#endif

        strcpy_P(buffer, PSTR("fan_speed="));
        ptr = buffer + strlen(buffer);
        int_to_string(eeprom_read_byte(EEPROM_MATERIAL_FAN_SPEED_OFFSET(n)), ptr, PSTR("\n"));
        card.write_string(buffer);

        strcpy_P(buffer, PSTR("flow="));
        ptr = buffer + strlen(buffer);
        int_to_string(eeprom_read_word(EEPROM_MATERIAL_FLOW_OFFSET(n)), ptr, PSTR("\n"));
        card.write_string(buffer);

        strcpy_P(buffer, PSTR("diameter="));
        ptr = buffer + strlen(buffer);
        float_to_string2(eeprom_read_float(EEPROM_MATERIAL_DIAMETER_OFFSET(n)), ptr, PSTR("\n"));
        card.write_string(buffer);

#ifdef USE_CHANGE_TEMPERATURE
        strcpy_P(buffer, PSTR("change_temp="));
        ptr = buffer + strlen(buffer);
        float_to_string2(eeprom_read_word(EEPROM_MATERIAL_CHANGE_TEMPERATURE(n)), ptr, PSTR("\n"));
        card.write_string(buffer);

        strcpy_P(buffer, PSTR("change_wait="));
        ptr = buffer + strlen(buffer);
        float_to_string2(eeprom_read_byte(EEPROM_MATERIAL_CHANGE_WAIT_TIME(n)), ptr, PSTR("\n\n"));
        card.write_string(buffer);
#endif
    }
    card.closefile();
    menu.replace_menu(menu_t(lcd_menu_material_export_done));
}

static void lcd_menu_material_import_done()
{
    lcd_lib_encoder_pos = MAIN_MENU_ITEM_POS(0);
    lcd_info_screen(NULL, lcd_change_to_previous_menu, PSTR("Ok"));
    lcd_lib_draw_string_centerP(20, PSTR("Loaded materials"));
    lcd_lib_draw_string_centerP(30, PSTR("from the SD card"));
    lcd_lib_update_screen();
}

static void lcd_menu_material_import()
{
    if (!card.sdInserted())
    {
        LED_GLOW
        lcd_lib_encoder_pos = MAIN_MENU_ITEM_POS(0);
        lcd_info_screen(NULL, lcd_change_to_previous_menu);
        lcd_lib_draw_string_centerP(15, PSTR("No SD-CARD!"));
        lcd_lib_draw_string_centerP(25, PSTR("Please insert card"));
        lcd_lib_update_screen();
        card.release();
        return;
    }
    if (!card.isOk())
    {
        lcd_info_screen(NULL, lcd_change_to_previous_menu);
        lcd_lib_draw_string_centerP(16, PSTR("Reading card..."));
        lcd_lib_update_screen();
        card.initsd();
        return;
    }

    card.setroot();
    card.openFile("MATERIAL.TXT", true);
    if (!card.isFileOpen())
    {
        lcd_info_screen(NULL, lcd_change_to_previous_menu);
        lcd_lib_draw_string_centerP(15, PSTR("No export file"));
        lcd_lib_draw_string_centerP(25, PSTR("Found on card."));
        lcd_lib_update_screen();
        return;
    }

    char buffer[32] = {0};
    uint8_t count = 0xFF;
    while(card.fgets(buffer, sizeof(buffer)) > 0)
    {
        buffer[sizeof(buffer)-1] = '\0';
        char* c = strchr(buffer, '\n');
        if (c) *c = '\0';

        if(strcmp_P(buffer, PSTR("[material]")) == 0)
        {
            count++;
        }else if (count < EEPROM_MATERIAL_SETTINGS_MAX_COUNT)
        {
            c = strchr(buffer, '=');
            if (c)
            {
                *c++ = '\0';
                if (strcmp_P(buffer, PSTR("name")) == 0)
                {
                    eeprom_write_block(c, EEPROM_MATERIAL_NAME_OFFSET(count), MATERIAL_NAME_SIZE);
                }else if (strcmp_P(buffer, PSTR("temperature")) == 0)
                {
                    eeprom_write_word(EEPROM_MATERIAL_TEMPERATURE_OFFSET(count), strtol(c, NULL, 10));
                }else if (strcmp_P(buffer, PSTR("bed_temperature")) == 0)
                {
                    eeprom_write_word(EEPROM_MATERIAL_BED_TEMPERATURE_OFFSET(count), strtol(c, NULL, 10));
                }else if (strcmp_P(buffer, PSTR("fan_speed")) == 0)
                {
                    eeprom_write_byte(EEPROM_MATERIAL_FAN_SPEED_OFFSET(count), strtol(c, NULL, 10));
                }else if (strcmp_P(buffer, PSTR("flow")) == 0)
                {
                    eeprom_write_word(EEPROM_MATERIAL_FLOW_OFFSET(count), strtol(c, NULL, 10));
                }else if (strcmp_P(buffer, PSTR("diameter")) == 0)
                {
                    eeprom_write_float(EEPROM_MATERIAL_DIAMETER_OFFSET(count), strtod(c, NULL));
#ifdef USE_CHANGE_TEMPERATURE
                }else if (strcmp_P(buffer, PSTR("change_temp")) == 0)
                {
                    eeprom_write_word(EEPROM_MATERIAL_CHANGE_TEMPERATURE(count), strtol(c, NULL, 10));
                }else if (strcmp_P(buffer, PSTR("change_wait")) == 0)
                {
                    eeprom_write_byte(EEPROM_MATERIAL_CHANGE_WAIT_TIME(count), strtol(c, NULL, 10));
#endif
                }
                for(uint8_t nozzle=0; nozzle<MATERIAL_TEMPERATURE_COUNT; ++nozzle)
                {
                    char buffer2[32];
                    strcpy_P(buffer2, PSTR("temperature_"));
                    char* ptr = buffer2 + strlen(buffer2);
                    float_to_string2(nozzleIndexToNozzleSize(nozzle), ptr);
                    if (strcmp(buffer, buffer2) == 0)
                    {
                        eeprom_write_word(EEPROM_MATERIAL_EXTRA_TEMPERATURE_OFFSET(count, nozzle), strtol(c, NULL, 10));
                    }
                }
            }
        }
    }
    count++;
    if (count > 0)
    {
        eeprom_write_byte(EEPROM_MATERIAL_COUNT_OFFSET(), count);
    }
    card.closefile();

    menu.replace_menu(menu_t(lcd_menu_material_import_done));
}

static void lcd_material_select_callback(uint8_t nr, uint8_t offsetY, uint8_t flags)
{
    uint8_t count = eeprom_read_byte(EEPROM_MATERIAL_COUNT_OFFSET());
    char buffer[32] = {0};
    if (nr == 0)
        lcd_cpyreturn(buffer);
    else if (nr == count + 1)
        strcpy_P(buffer, PSTR("Customize"));
    else if (nr == count + 2)
        strcpy_P(buffer, PSTR("Export to SD"));
    else if (nr == count + 3)
        strcpy_P(buffer, PSTR("Import from SD"));
    else{
        eeprom_read_block(buffer, EEPROM_MATERIAL_NAME_OFFSET(nr - 1), MATERIAL_NAME_SIZE);
        buffer[MATERIAL_NAME_SIZE] = '\0';
    }
    lcd_draw_scroll_entry(offsetY, buffer, flags);
}

static void lcd_material_select_details_callback(uint8_t nr)
{
    uint8_t count = eeprom_read_byte(EEPROM_MATERIAL_COUNT_OFFSET());
    if (nr == 0)
    {

    }
    else if (nr <= count)
    {
        char buffer[32] = {0};
        char* c = buffer;
        nr -= 1;

        if (led_glow_dir)
        {
            c = float_to_string2(eeprom_read_float(EEPROM_MATERIAL_DIAMETER_OFFSET(nr)), c, PSTR("mm"));
            while(c < buffer + 10) *c++ = ' ';
            strcpy_P(c, PSTR("Flow:"));
            c += 5;
            c = int_to_string(eeprom_read_word(EEPROM_MATERIAL_FLOW_OFFSET(nr)), c, PSTR("%"));
        }else{
            c = int_to_string(eeprom_read_word(EEPROM_MATERIAL_TEMPERATURE_OFFSET(nr)), c, PSTR("C"));
#if TEMP_SENSOR_BED != 0
            *c++ = ' ';
            c = int_to_string(eeprom_read_word(EEPROM_MATERIAL_BED_TEMPERATURE_OFFSET(nr)), c, PSTR("C"));
#endif
            while(c < buffer + 10) *c++ = ' ';
            strcpy_P(c, PSTR("Fan: "));
            c += 5;
            c = int_to_string(eeprom_read_byte(EEPROM_MATERIAL_FAN_SPEED_OFFSET(nr)), c, PSTR("%"));
        }
        lcd_lib_draw_string_left(BOTTOM_MENU_YPOS, buffer);
    }else if (nr == count + 1)
    {
        lcd_lib_draw_string_centerP(BOTTOM_MENU_YPOS, PSTR("Modify the settings"));
    }else if (nr == count + 2)
    {
        lcd_lib_draw_string_centerP(BOTTOM_MENU_YPOS, PSTR("Saves all materials"));
    }else if (nr == count + 3)
    {
        lcd_lib_draw_string_centerP(BOTTOM_MENU_YPOS, PSTR("Loads all materials"));
    }
}

void lcd_menu_material_select()
{
    uint8_t count = eeprom_read_byte(EEPROM_MATERIAL_COUNT_OFFSET());

    lcd_scroll_menu(PSTR("MATERIAL"), count + 4, lcd_material_select_callback, lcd_material_select_details_callback);
    if (lcd_lib_button_pressed)
    {
        if (IS_SELECTED_SCROLL(0))
            menu.return_to_previous();
        else if (IS_SELECTED_SCROLL(count + 1))
            menu.add_menu(menu_t(lcd_menu_material_settings));
        else if (IS_SELECTED_SCROLL(count + 2))
            menu.add_menu(menu_t(lcd_menu_material_export));
        else if (IS_SELECTED_SCROLL(count + 3))
            menu.add_menu(menu_t(lcd_menu_material_import));
        else{
            lcd_material_set_material(SELECTED_SCROLL_MENU_ITEM() - 1, menu_extruder);
            menu.replace_menu(menu_t(lcd_menu_material_selected, MAIN_MENU_ITEM_POS(0)));
        }
    }
    lcd_lib_update_screen();
}

static void lcd_menu_material_selected()
{
    lcd_info_screen(NULL, lcd_change_to_previous_menu, PSTR("OK"));
    lcd_lib_draw_string_centerP(20, PSTR("Selected material:"));
    lcd_lib_draw_string_center(30, LCD_CACHE_FILENAME(0));
#if EXTRUDERS > 1
    if (menu_extruder == 0)
        lcd_lib_draw_string_centerP(40, PSTR("for extruder 1"));
    else if (menu_extruder == 1)
        lcd_lib_draw_string_centerP(40, PSTR("for extruder 2"));
#endif
    lcd_lib_update_screen();
}

static void lcd_material_settings_callback(uint8_t nr, uint8_t offsetY, uint8_t flags)
{
    char buffer[32] = {0};
    if (nr == 0)
        lcd_cpyreturn(buffer);
    else if (nr == 1)
        strcpy_P(buffer, PSTR("Temperature"));
#if TEMP_SENSOR_BED != 0
    else if (nr == 2)
        strcpy_P(buffer, PSTR("Heated buildplate"));
#endif
    else if (nr == 2 + BED_MENU_OFFSET)
        strcpy_P(buffer, PSTR("Diameter"));
    else if (nr == 3 + BED_MENU_OFFSET)
        strcpy_P(buffer, PSTR("Fan"));
    else if (nr == 4 + BED_MENU_OFFSET)
        strcpy_P(buffer, PSTR("Flow %"));
#ifdef USE_CHANGE_TEMPERATURE
    else if (nr == 5 + BED_MENU_OFFSET)
        strcpy_P(buffer, PSTR("Change temperature"));
    else if (nr == 6 + BED_MENU_OFFSET)
        strcpy_P(buffer, PSTR("Change wait time"));
    else if (nr == 7 + BED_MENU_OFFSET)
        strcpy_P(buffer, PSTR("Store as preset"));
#else
    else if (nr == 5 + BED_MENU_OFFSET)
        strcpy_P(buffer, PSTR("Store as preset"));
#endif

    lcd_draw_scroll_entry(offsetY, buffer, flags);
}

static void lcd_material_settings_details_callback(uint8_t nr)
{
    char buffer[20];
    buffer[0] = '\0';
    if (nr == 0)
    {
        return;
    }else if (nr == 1)
    {
        char* c = buffer;
        if (led_glow_dir)
        {
            for(uint8_t n=0; n<3; ++n)
                c = int_to_string(material[menu_extruder].temperature[n], c, PSTR("C "));
        }else{
            for(uint8_t n=3; n<MATERIAL_TEMPERATURE_COUNT; ++n)
                c = int_to_string(material[menu_extruder].temperature[n], c, PSTR("C "));
        }
#if TEMP_SENSOR_BED != 0
    }else if (nr == 2)
    {
        int_to_string(material[menu_extruder].bed_temperature, buffer, PSTR("C"));
#endif
    }else if (nr == 2 + BED_MENU_OFFSET)
    {
        float_to_string2(material[menu_extruder].diameter, buffer, PSTR("mm"));
    }else if (nr == 3 + BED_MENU_OFFSET)
    {
        int_to_string(material[menu_extruder].fan_speed, buffer, PSTR("%"));
    }else if (nr == 4 + BED_MENU_OFFSET)
    {
        int_to_string(material[menu_extruder].flow, buffer, PSTR("%"));
#ifdef USE_CHANGE_TEMPERATURE
    }else if (nr == 5 + BED_MENU_OFFSET)
    {
        int_to_string(material[menu_extruder].change_temperature, buffer, PSTR("C"));
    }else if (nr == 6 + BED_MENU_OFFSET)
    {
        int_to_string(material[menu_extruder].change_preheat_wait_time, buffer, PSTR("Sec"));
#endif
    }
    lcd_lib_draw_string_left(BOTTOM_MENU_YPOS, buffer);
}

static void lcd_menu_material_settings()
{
#ifdef USE_CHANGE_TEMPERATURE
    lcd_scroll_menu(PSTR("MATERIAL"), 8 + BED_MENU_OFFSET, lcd_material_settings_callback, lcd_material_settings_details_callback);
#else
    lcd_scroll_menu(PSTR("MATERIAL"), 6 + BED_MENU_OFFSET, lcd_material_settings_callback, lcd_material_settings_details_callback);
#endif
    if (lcd_lib_button_pressed)
    {
        if (IS_SELECTED_SCROLL(0))
        {
            lcd_change_to_previous_menu();
            lcd_material_store_current_material();
        }else if (IS_SELECTED_SCROLL(1))
        {
            //LCD_EDIT_SETTING(material[menu_extruder].temperature[0], "Temperature", "C", 0, HEATER_0_MAXTEMP - 15);
            menu.add_menu(menu_t(lcd_menu_material_temperature_settings));
        }
#if TEMP_SENSOR_BED != 0
        else if (IS_SELECTED_SCROLL(2))
            LCD_EDIT_SETTING(material[menu_extruder].bed_temperature, "Buildplate Temp.", "C", 0, BED_MAXTEMP - 15);
#endif
        else if (IS_SELECTED_SCROLL(2 + BED_MENU_OFFSET))
            LCD_EDIT_SETTING_FLOAT001(material[menu_extruder].diameter, "Material Diameter", "mm", 0, 100);
        else if (IS_SELECTED_SCROLL(3 + BED_MENU_OFFSET))
            LCD_EDIT_SETTING(material[menu_extruder].fan_speed, "Fan speed", "%", 0, 100);
        else if (IS_SELECTED_SCROLL(4 + BED_MENU_OFFSET))
            LCD_EDIT_SETTING(material[menu_extruder].flow, "Material flow", "%", 1, 1000);
#ifdef USE_CHANGE_TEMPERATURE
        else if (IS_SELECTED_SCROLL(5 + BED_MENU_OFFSET))
            LCD_EDIT_SETTING(material[menu_extruder].change_temperature, "Change temperature", "C", 0, get_maxtemp(menu_extruder));
        else if (IS_SELECTED_SCROLL(6 + BED_MENU_OFFSET))
            LCD_EDIT_SETTING(material[menu_extruder].change_preheat_wait_time, "Change wait time", "sec", 0, 180);
        else if (IS_SELECTED_SCROLL(7 + BED_MENU_OFFSET))
            menu.add_menu(menu_t(lcd_menu_material_settings_store));
#else
        else if (IS_SELECTED_SCROLL(5 + BED_MENU_OFFSET))
            menu.add_menu(menu_t(lcd_menu_material_settings_store));
#endif
    }
    lcd_lib_update_screen();
}

static void lcd_material_temperature_settings_callback(uint8_t nr, uint8_t offsetY, uint8_t flags)
{
    char buffer[20] = {0};
    if (nr == 0)
    {
        lcd_cpyreturn(buffer);
    }
    else
    {
        strcpy_P(buffer, PSTR("Temperature: "));
        float_to_string2(nozzleIndexToNozzleSize(nr - 1), buffer + strlen(buffer));
    }

    lcd_draw_scroll_entry(offsetY, buffer, flags);
}

static void lcd_material_settings_temperature_details_callback(uint8_t nr)
{
    if (nr > 0)
    {
        char buffer[10] = {0};
        int_to_string(material[menu_extruder].temperature[nr - 1], buffer, PSTR("C"));
        lcd_lib_draw_string_left(BOTTOM_MENU_YPOS, buffer);
    }
}

static void lcd_menu_material_temperature_settings()
{
    lcd_scroll_menu(PSTR("MATERIAL"), 1 + MATERIAL_TEMPERATURE_COUNT, lcd_material_temperature_settings_callback, lcd_material_settings_temperature_details_callback);
    if (lcd_lib_button_pressed)
    {
        if (IS_SELECTED_SCROLL(0))
        {
            menu.return_to_previous();
        }
        else
        {
            uint8_t index = SELECTED_SCROLL_MENU_ITEM() - 1;
            menu.return_to_previous();
            LCD_EDIT_SETTING(material[menu_extruder].temperature[index], "Temperature", "C", 0, HEATER_0_MAXTEMP - 15);
        }
    }
    lcd_lib_update_screen();
}

static void lcd_menu_material_settings_store_callback(uint8_t nr, uint8_t offsetY, uint8_t flags)
{
    uint8_t count = eeprom_read_byte(EEPROM_MATERIAL_COUNT_OFFSET());
    char buffer[32] = {0};
    if (nr == 0)
        lcd_cpyreturn(buffer);
    else if (nr > count)
        strcpy_P(buffer, PSTR("New preset"));
    else{
        eeprom_read_block(buffer, EEPROM_MATERIAL_NAME_OFFSET(nr - 1), MATERIAL_NAME_SIZE);
        buffer[MATERIAL_NAME_SIZE] = '\0';
    }
    lcd_draw_scroll_entry(offsetY, buffer, flags);
}

static void lcd_menu_material_settings_store_details_callback(uint8_t nr)
{
}

static void lcd_menu_material_settings_store()
{
    uint8_t count = eeprom_read_byte(EEPROM_MATERIAL_COUNT_OFFSET());
    if (count == EEPROM_MATERIAL_SETTINGS_MAX_COUNT)
        count--;
    lcd_scroll_menu(PSTR("PRESETS"), 2 + count, lcd_menu_material_settings_store_callback, lcd_menu_material_settings_store_details_callback);

    if (lcd_lib_button_pressed)
    {
        if (!IS_SELECTED_SCROLL(0))
        {
            uint8_t idx = SELECTED_SCROLL_MENU_ITEM() - 1;
            if (idx == count)
            {
                char buffer[9] = "CUSTOM";
                int_to_string(idx - 1, buffer + 6);
                eeprom_write_block(buffer, EEPROM_MATERIAL_NAME_OFFSET(idx), MATERIAL_NAME_SIZE);
                eeprom_write_byte(EEPROM_MATERIAL_COUNT_OFFSET(), idx + 1);
            }
            lcd_material_store_material(idx);
        }
        lcd_change_to_previous_menu();
    }
    lcd_lib_update_screen();
}

void lcd_material_reset_defaults()
{
    //Fill in the defaults
    char buffer[MATERIAL_NAME_SIZE+1] = {0};

    strcpy_P(buffer, PSTR("PLA"));
    eeprom_write_block(buffer, EEPROM_MATERIAL_NAME_OFFSET(0), 4);
    eeprom_write_word(EEPROM_MATERIAL_TEMPERATURE_OFFSET(0), 210);
    eeprom_write_word(EEPROM_MATERIAL_BED_TEMPERATURE_OFFSET(0), 60);
    eeprom_write_byte(EEPROM_MATERIAL_FAN_SPEED_OFFSET(0), 100);
    eeprom_write_word(EEPROM_MATERIAL_FLOW_OFFSET(0), 100);
    eeprom_write_float(EEPROM_MATERIAL_DIAMETER_OFFSET(0), 2.85);

    eeprom_write_word(EEPROM_MATERIAL_EXTRA_TEMPERATURE_OFFSET(0, 0), 210);//0.4
    eeprom_write_word(EEPROM_MATERIAL_EXTRA_TEMPERATURE_OFFSET(0, 1), 195);//0.25
    eeprom_write_word(EEPROM_MATERIAL_EXTRA_TEMPERATURE_OFFSET(0, 2), 230);//0.6
    eeprom_write_word(EEPROM_MATERIAL_EXTRA_TEMPERATURE_OFFSET(0, 3), 240);//0.8
    eeprom_write_word(EEPROM_MATERIAL_EXTRA_TEMPERATURE_OFFSET(0, 4), 240);//1.0

    eeprom_write_word(EEPROM_MATERIAL_CHANGE_TEMPERATURE(0), 70);
    eeprom_write_byte(EEPROM_MATERIAL_CHANGE_WAIT_TIME(0), 30);

    strcpy_P(buffer, PSTR("ABS"));
    eeprom_write_block(buffer, EEPROM_MATERIAL_NAME_OFFSET(1), 4);
    eeprom_write_word(EEPROM_MATERIAL_TEMPERATURE_OFFSET(1), 260);
    eeprom_write_word(EEPROM_MATERIAL_BED_TEMPERATURE_OFFSET(1), 90);
    eeprom_write_byte(EEPROM_MATERIAL_FAN_SPEED_OFFSET(1), 100);
    eeprom_write_word(EEPROM_MATERIAL_FLOW_OFFSET(1), 107);
    eeprom_write_float(EEPROM_MATERIAL_DIAMETER_OFFSET(1), 2.85);

    eeprom_write_word(EEPROM_MATERIAL_EXTRA_TEMPERATURE_OFFSET(1, 0), 255);//0.4
    eeprom_write_word(EEPROM_MATERIAL_EXTRA_TEMPERATURE_OFFSET(1, 1), 245);//0.25
    eeprom_write_word(EEPROM_MATERIAL_EXTRA_TEMPERATURE_OFFSET(1, 2), 260);//0.6
    eeprom_write_word(EEPROM_MATERIAL_EXTRA_TEMPERATURE_OFFSET(1, 3), 260);//0.8
    eeprom_write_word(EEPROM_MATERIAL_EXTRA_TEMPERATURE_OFFSET(1, 4), 260);//1.0

    eeprom_write_word(EEPROM_MATERIAL_CHANGE_TEMPERATURE(1), 90);
    eeprom_write_byte(EEPROM_MATERIAL_CHANGE_WAIT_TIME(1), 30);

    strcpy_P(buffer, PSTR("CPE"));
    eeprom_write_block(buffer, EEPROM_MATERIAL_NAME_OFFSET(2), 4);
    eeprom_write_word(EEPROM_MATERIAL_TEMPERATURE_OFFSET(2), 255);
    eeprom_write_word(EEPROM_MATERIAL_BED_TEMPERATURE_OFFSET(2), 60);
    eeprom_write_byte(EEPROM_MATERIAL_FAN_SPEED_OFFSET(2), 50);
    eeprom_write_word(EEPROM_MATERIAL_FLOW_OFFSET(2), 100);
    eeprom_write_float(EEPROM_MATERIAL_DIAMETER_OFFSET(2), 2.85);

    eeprom_write_word(EEPROM_MATERIAL_EXTRA_TEMPERATURE_OFFSET(2, 0), 255);//0.4
    eeprom_write_word(EEPROM_MATERIAL_EXTRA_TEMPERATURE_OFFSET(2, 1), 245);//0.25
    eeprom_write_word(EEPROM_MATERIAL_EXTRA_TEMPERATURE_OFFSET(2, 2), 260);//0.6
    eeprom_write_word(EEPROM_MATERIAL_EXTRA_TEMPERATURE_OFFSET(2, 3), 260);//0.8
    eeprom_write_word(EEPROM_MATERIAL_EXTRA_TEMPERATURE_OFFSET(2, 4), 260);//1.0

    eeprom_write_word(EEPROM_MATERIAL_CHANGE_TEMPERATURE(2), 85);
    eeprom_write_byte(EEPROM_MATERIAL_CHANGE_WAIT_TIME(2), 15);

    eeprom_write_byte(EEPROM_MATERIAL_COUNT_OFFSET(), 3);

    for(uint8_t n=MATERIAL_TEMPERATURE_COUNT; n<MAX_MATERIAL_TEMPERATURES; ++n)
    {
        eeprom_write_word(EEPROM_MATERIAL_EXTRA_TEMPERATURE_OFFSET(0, n), 0);
        eeprom_write_word(EEPROM_MATERIAL_EXTRA_TEMPERATURE_OFFSET(1, n), 0);
        eeprom_write_word(EEPROM_MATERIAL_EXTRA_TEMPERATURE_OFFSET(2, n), 0);
    }
}

void lcd_material_set_material(uint8_t nr, uint8_t e)
{
    material[e].temperature[0] = eeprom_read_word(EEPROM_MATERIAL_TEMPERATURE_OFFSET(nr));
    set_maxtemp(e, constrain(material[e].temperature[0] + 15, HEATER_0_MAXTEMP, min(HEATER_0_MAXTEMP + 15, material[e].temperature[0] + 15)));

#if TEMP_SENSOR_BED != 0
    material[e].bed_temperature = eeprom_read_word(EEPROM_MATERIAL_BED_TEMPERATURE_OFFSET(nr));
    if (material[e].bed_temperature > BED_MAXTEMP - 15)
        material[e].bed_temperature = BED_MAXTEMP - 15;
#endif
    material[e].flow = eeprom_read_word(EEPROM_MATERIAL_FLOW_OFFSET(nr));

    material[e].fan_speed = eeprom_read_byte(EEPROM_MATERIAL_FAN_SPEED_OFFSET(nr));
    material[e].diameter = eeprom_read_float(EEPROM_MATERIAL_DIAMETER_OFFSET(nr));

    eeprom_read_block(material[e].name, EEPROM_MATERIAL_NAME_OFFSET(nr), MATERIAL_NAME_SIZE);
    material[e].name[MATERIAL_NAME_SIZE] = '\0';
    strcpy(LCD_CACHE_FILENAME(0), material[e].name);
    for(uint8_t n=0; n<MAX_MATERIAL_TEMPERATURES; ++n)
    {
        material[e].temperature[n] = eeprom_read_word(EEPROM_MATERIAL_EXTRA_TEMPERATURE_OFFSET(nr, n));
//        set_maxtemp(e, constrain(material[e].temperature[n] + 15, HEATER_0_MAXTEMP, min(max(get_maxtemp(e), HEATER_0_MAXTEMP + 15), material[e].temperature[n] + 15)));
        if (material[e].temperature[n] > get_maxtemp(e) - 15)
            material[e].temperature[n] = get_maxtemp(e) - 15;
    }

#if TEMP_SENSOR_BED != 0
    if (material[e].bed_temperature > BED_MAXTEMP - 15)
        material[e].bed_temperature = BED_MAXTEMP - 15;
#endif
    material[e].change_temperature = eeprom_read_word(EEPROM_MATERIAL_CHANGE_TEMPERATURE(nr));
    material[e].change_preheat_wait_time = eeprom_read_byte(EEPROM_MATERIAL_CHANGE_WAIT_TIME(nr));
    if ((material[e].change_temperature < 10) || (material[e].change_temperature > (get_maxtemp(e) - 15)))
        material[e].change_temperature = material[e].temperature[0];

    lcd_material_store_current_material();
}

void lcd_material_store_material(uint8_t nr)
{
    eeprom_write_word(EEPROM_MATERIAL_TEMPERATURE_OFFSET(nr), material[menu_extruder].temperature[0]);
#if TEMP_SENSOR_BED != 0
    eeprom_write_word(EEPROM_MATERIAL_BED_TEMPERATURE_OFFSET(nr), material[menu_extruder].bed_temperature);
#endif
    eeprom_write_word(EEPROM_MATERIAL_FLOW_OFFSET(nr), material[menu_extruder].flow);

    eeprom_write_byte(EEPROM_MATERIAL_FAN_SPEED_OFFSET(nr), material[menu_extruder].fan_speed);
    eeprom_write_float(EEPROM_MATERIAL_DIAMETER_OFFSET(nr), material[menu_extruder].diameter);
    for(uint8_t n=0; n<MAX_MATERIAL_TEMPERATURES; ++n)
        eeprom_write_word(EEPROM_MATERIAL_EXTRA_TEMPERATURE_OFFSET(nr, n), material[menu_extruder].temperature[n]);

    eeprom_write_word(EEPROM_MATERIAL_CHANGE_TEMPERATURE(nr), material[menu_extruder].change_temperature);
    eeprom_write_byte(EEPROM_MATERIAL_CHANGE_WAIT_TIME(nr), material[menu_extruder].change_preheat_wait_time);
}

void lcd_material_read_current_material()
{
    for(uint8_t e=0; e<EXTRUDERS; ++e)
    {
        material[e].temperature[0] = eeprom_read_word(EEPROM_MATERIAL_TEMPERATURE_OFFSET(EEPROM_MATERIAL_SETTINGS_MAX_COUNT+e));
        set_maxtemp(e, constrain(material[e].temperature[0] + 15, HEATER_0_MAXTEMP, min(HEATER_0_MAXTEMP + 15, material[e].temperature[0] + 15)));
#if TEMP_SENSOR_BED != 0
        material[e].bed_temperature = eeprom_read_word(EEPROM_MATERIAL_BED_TEMPERATURE_OFFSET(EEPROM_MATERIAL_SETTINGS_MAX_COUNT+e));
#endif
        material[e].flow = eeprom_read_word(EEPROM_MATERIAL_FLOW_OFFSET(EEPROM_MATERIAL_SETTINGS_MAX_COUNT+e));

        material[e].fan_speed = eeprom_read_byte(EEPROM_MATERIAL_FAN_SPEED_OFFSET(EEPROM_MATERIAL_SETTINGS_MAX_COUNT+e));
        material[e].diameter = eeprom_read_float(EEPROM_MATERIAL_DIAMETER_OFFSET(EEPROM_MATERIAL_SETTINGS_MAX_COUNT+e));
        for(uint8_t n=0; n<MAX_MATERIAL_TEMPERATURES; ++n)
        {
            material[e].temperature[n] = eeprom_read_word(EEPROM_MATERIAL_EXTRA_TEMPERATURE_OFFSET(EEPROM_MATERIAL_SETTINGS_MAX_COUNT+e, n));
            // set_maxtemp(e, constrain(material[e].temperature[n] + 15, HEATER_0_MAXTEMP, min(HEATER_0_MAXTEMP + 15, material[e].temperature[n] + 15)));
        }

        eeprom_read_block(material[e].name, EEPROM_MATERIAL_NAME_OFFSET(EEPROM_MATERIAL_SETTINGS_MAX_COUNT+e), MATERIAL_NAME_SIZE);
        material[e].name[MATERIAL_NAME_SIZE] = '\0';

        material[e].change_temperature = eeprom_read_word(EEPROM_MATERIAL_CHANGE_TEMPERATURE(EEPROM_MATERIAL_SETTINGS_MAX_COUNT+e));
        material[e].change_preheat_wait_time = eeprom_read_byte(EEPROM_MATERIAL_CHANGE_WAIT_TIME(EEPROM_MATERIAL_SETTINGS_MAX_COUNT+e));
        if ((material[e].change_temperature < 10) || (material[e].change_temperature > (get_maxtemp(e) - 15)))
            material[e].change_temperature = material[e].temperature[0];
    }
}

void lcd_material_store_current_material()
{
    for(uint8_t e=0; e<EXTRUDERS; ++e)
    {
        eeprom_write_word(EEPROM_MATERIAL_TEMPERATURE_OFFSET(EEPROM_MATERIAL_SETTINGS_MAX_COUNT+e), material[e].temperature[0]);
        set_maxtemp(e, constrain(material[e].temperature[0] + 15, HEATER_0_MAXTEMP, min(HEATER_0_MAXTEMP + 15, material[e].temperature[0] + 15)));
#if TEMP_SENSOR_BED != 0
        eeprom_write_word(EEPROM_MATERIAL_BED_TEMPERATURE_OFFSET(EEPROM_MATERIAL_SETTINGS_MAX_COUNT+e), material[e].bed_temperature);
#endif
        eeprom_write_byte(EEPROM_MATERIAL_FAN_SPEED_OFFSET(EEPROM_MATERIAL_SETTINGS_MAX_COUNT+e), material[e].fan_speed);
        eeprom_write_word(EEPROM_MATERIAL_FLOW_OFFSET(EEPROM_MATERIAL_SETTINGS_MAX_COUNT+e), material[e].flow);
        eeprom_write_float(EEPROM_MATERIAL_DIAMETER_OFFSET(EEPROM_MATERIAL_SETTINGS_MAX_COUNT+e), material[e].diameter);

        for(uint8_t n=0; n<MAX_MATERIAL_TEMPERATURES; ++n)
        {
            eeprom_write_word(EEPROM_MATERIAL_EXTRA_TEMPERATURE_OFFSET(EEPROM_MATERIAL_SETTINGS_MAX_COUNT+e, n), material[e].temperature[n]);
            // set_maxtemp(e, constrain(material[e].temperature[n] + 15, HEATER_0_MAXTEMP, min(max(get_maxtemp(e), HEATER_0_MAXTEMP + 15), material[e].temperature[n] + 15)));
        }

        eeprom_write_block(material[e].name, EEPROM_MATERIAL_NAME_OFFSET(EEPROM_MATERIAL_SETTINGS_MAX_COUNT+e), MATERIAL_NAME_SIZE);


        eeprom_write_word(EEPROM_MATERIAL_CHANGE_TEMPERATURE(EEPROM_MATERIAL_SETTINGS_MAX_COUNT+e), material[e].change_temperature);
        eeprom_write_byte(EEPROM_MATERIAL_CHANGE_WAIT_TIME(EEPROM_MATERIAL_SETTINGS_MAX_COUNT+e), material[e].change_preheat_wait_time);
    }
}

bool lcd_material_verify_material_settings()
{
    bool hasCPE = false;
    char buffer[MATERIAL_NAME_SIZE+1] = {0};

    uint8_t cnt = eeprom_read_byte(EEPROM_MATERIAL_COUNT_OFFSET());
    if (cnt < 2 || cnt > EEPROM_MATERIAL_SETTINGS_MAX_COUNT)
        return false;
    while(cnt > 0)
    {
        cnt --;
        if (eeprom_read_word(EEPROM_MATERIAL_TEMPERATURE_OFFSET(cnt)) > HEATER_0_MAXTEMP)
            return false;
#if TEMP_SENSOR_BED != 0
        if (eeprom_read_word(EEPROM_MATERIAL_BED_TEMPERATURE_OFFSET(cnt)) > BED_MAXTEMP)
            return false;
#endif
        if (eeprom_read_byte(EEPROM_MATERIAL_FAN_SPEED_OFFSET(cnt)) > 100)
            return false;
        if (eeprom_read_word(EEPROM_MATERIAL_FLOW_OFFSET(cnt)) > 1000)
            return false;
        if (eeprom_read_float(EEPROM_MATERIAL_DIAMETER_OFFSET(cnt)) > 10.0)
            return false;
        if (eeprom_read_float(EEPROM_MATERIAL_DIAMETER_OFFSET(cnt)) < 0.1)
            return false;

        for(uint8_t n=0; n<MATERIAL_TEMPERATURE_COUNT; ++n)
        {
            if (eeprom_read_word(EEPROM_MATERIAL_EXTRA_TEMPERATURE_OFFSET(cnt, n)) > HEATER_0_MAXTEMP)
                return false;
            if (eeprom_read_word(EEPROM_MATERIAL_EXTRA_TEMPERATURE_OFFSET(cnt, n)) == 0)
                return false;
        }

        eeprom_read_block(buffer, EEPROM_MATERIAL_NAME_OFFSET(cnt), MATERIAL_NAME_SIZE);
        buffer[MATERIAL_NAME_SIZE] = '\0';
        if (strcmp_P(buffer, PSTR("UPET")) == 0)
        {
            strcpy_P(buffer, PSTR("CPE"));
            eeprom_write_block(buffer, EEPROM_MATERIAL_NAME_OFFSET(cnt), 4);
        }
        if (strcmp_P(buffer, PSTR("CPE")) == 0)
        {
            hasCPE = true;
        }

        if (eeprom_read_word(EEPROM_MATERIAL_CHANGE_TEMPERATURE(cnt)) > HEATER_0_MAXTEMP || eeprom_read_word(EEPROM_MATERIAL_CHANGE_TEMPERATURE(cnt)) < 10)
        {
            //Invalid temperature for change temperature.
            if (strcmp_P(buffer, PSTR("PLA")) == 0)
            {
                eeprom_write_word(EEPROM_MATERIAL_CHANGE_TEMPERATURE(cnt), 70);
                eeprom_write_byte(EEPROM_MATERIAL_CHANGE_WAIT_TIME(cnt), 30);
            }
            else if (strcmp_P(buffer, PSTR("ABS")) == 0)
            {
                eeprom_write_word(EEPROM_MATERIAL_CHANGE_TEMPERATURE(cnt), 90);
                eeprom_write_byte(EEPROM_MATERIAL_CHANGE_WAIT_TIME(cnt), 30);
            }
            else if (strcmp_P(buffer, PSTR("CPE")) == 0)
            {
                eeprom_write_word(EEPROM_MATERIAL_CHANGE_TEMPERATURE(cnt), 85);
                eeprom_write_byte(EEPROM_MATERIAL_CHANGE_WAIT_TIME(cnt), 15);
            }
            else
            {
                eeprom_write_word(EEPROM_MATERIAL_CHANGE_TEMPERATURE(cnt), eeprom_read_word(EEPROM_MATERIAL_TEMPERATURE_OFFSET(cnt)));
                eeprom_write_byte(EEPROM_MATERIAL_CHANGE_WAIT_TIME(cnt), 5);
            }
        }
    }
    cnt = eeprom_read_byte(EEPROM_MATERIAL_COUNT_OFFSET());
    if (!hasCPE && cnt < EEPROM_MATERIAL_SETTINGS_MAX_COUNT)
    {
        strcpy_P(buffer, PSTR("CPE"));
        eeprom_write_block(buffer, EEPROM_MATERIAL_NAME_OFFSET(cnt), 4);
        eeprom_write_word(EEPROM_MATERIAL_TEMPERATURE_OFFSET(cnt), 250);
        eeprom_write_word(EEPROM_MATERIAL_BED_TEMPERATURE_OFFSET(cnt), 60);
        eeprom_write_byte(EEPROM_MATERIAL_FAN_SPEED_OFFSET(cnt), 50);
        eeprom_write_word(EEPROM_MATERIAL_FLOW_OFFSET(cnt), 100);
        eeprom_write_float(EEPROM_MATERIAL_DIAMETER_OFFSET(cnt), 2.85);
        eeprom_write_word(EEPROM_MATERIAL_EXTRA_TEMPERATURE_OFFSET(cnt, 0), 255);//0.4
        eeprom_write_word(EEPROM_MATERIAL_EXTRA_TEMPERATURE_OFFSET(cnt, 1), 245);//0.25
        eeprom_write_word(EEPROM_MATERIAL_EXTRA_TEMPERATURE_OFFSET(cnt, 2), 260);//0.6
        eeprom_write_word(EEPROM_MATERIAL_EXTRA_TEMPERATURE_OFFSET(cnt, 3), 260);//0.8
        eeprom_write_word(EEPROM_MATERIAL_EXTRA_TEMPERATURE_OFFSET(cnt, 4), 260);//1.0

        eeprom_write_word(EEPROM_MATERIAL_CHANGE_TEMPERATURE(cnt), 85);
        eeprom_write_byte(EEPROM_MATERIAL_CHANGE_WAIT_TIME(cnt), 15);

        eeprom_write_byte(EEPROM_MATERIAL_COUNT_OFFSET(), cnt + 1);
    }
    return true;
}

uint8_t nozzleSizeToTemperatureIndex(float nozzle_size)
{
    if (fabs(nozzle_size - 0.25) < 0.1)
        return 1;
    if (fabs(nozzle_size - 0.60) < 0.1)
        return 2;
    if (fabs(nozzle_size - 0.80) < 0.1)
        return 3;
    if (fabs(nozzle_size - 1.00) < 0.1)
        return 4;

    //Default to index 0
    return 0;
}

float nozzleIndexToNozzleSize(uint8_t nozzle_index)
{
    switch(nozzle_index)
    {
    case 0:
        return 0.4;
    case 1:
        return 0.25;
    case 2:
        return 0.6;
    case 3:
        return 0.8;
    case 4:
        return 1.0;
    }
    return 0.0;
}

#endif//ENABLE_ULTILCD2
