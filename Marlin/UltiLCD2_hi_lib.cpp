#include <avr/pgmspace.h>
#include <string.h>

#include "Configuration.h"
#ifdef ENABLE_ULTILCD2
#include "UltiLCD2.h"
#include "UltiLCD2_hi_lib.h"
#include "temperature.h"
#include "UltiLCD2_menu_utils.h"
#if EXTRUDERS > 1
#include "UltiLCD2_menu_dual.h"
#endif

menuFunc_t postMenuCheck;
uint8_t minProgress;

const char* lcd_setting_name;
const char* lcd_setting_postfix;
void* lcd_setting_ptr;
uint8_t lcd_setting_type;
int16_t lcd_setting_min;
int16_t lcd_setting_max;
int16_t lcd_setting_start_value;

uint16_t lineEntryPos  = 0;
int8_t   lineEntryWait = 0;

uint8_t heater_timeout = 3;
uint16_t backup_temperature[EXTRUDERS] = { 0 };

//Arduino IDE compatibility, lacks the eeprom_read_float function
float eeprom_read_float(const float* addr)
{
    union { uint32_t i; float f; } n;
    n.i = eeprom_read_dword((uint32_t*)addr);
    return n.f;
}

void eeprom_write_float(const float* addr, float f)
{
    union { uint32_t i; float f; } n;
    n.f = f;
    eeprom_write_dword((uint32_t*)addr, n.i);
}

void lcd_tripple_menu(const char* left, const char* right, const char* bottom)
{
    if (lcd_lib_encoder_pos != ENCODER_NO_SELECTION)
    {
        if (lcd_lib_encoder_pos < 0)
            lcd_lib_encoder_pos += 3*ENCODER_TICKS_PER_MAIN_MENU_ITEM;
        if (lcd_lib_encoder_pos >= 3*ENCODER_TICKS_PER_MAIN_MENU_ITEM)
            lcd_lib_encoder_pos -= 3*ENCODER_TICKS_PER_MAIN_MENU_ITEM;
    }

    lcd_lib_clear();
    lcd_lib_draw_vline(64, 5, 46);
    lcd_lib_draw_hline(3, 124, 50);

    if (IS_SELECTED_MAIN(0))
    {
        lcd_lib_draw_box(3+2, 5+2, 64-3-2, 45-2);
        lcd_lib_set(3+3, 5+3, 64-3-3, 45-3);
        lcd_lib_clear_string_center_atP(33, 22, left);
    }else{
        lcd_lib_draw_string_center_atP(33, 22, left);
    }

    if (IS_SELECTED_MAIN(1))
    {
        lcd_lib_draw_box(64+3+2, 5+2, 125-2, 45-2);
        lcd_lib_set(64+3+3, 5+3, 125-3, 45-3);
        lcd_lib_clear_string_center_atP(64 + 33, 22, right);
    }else{
        lcd_lib_draw_string_center_atP(64 + 33, 22, right);
    }

    if (bottom != NULL)
    {
        if (IS_SELECTED_MAIN(2))
        {
            lcd_lib_draw_box(3+2, BOTTOM_MENU_YPOS-1, 124-2, BOTTOM_MENU_YPOS+7);
            lcd_lib_set(3+3, BOTTOM_MENU_YPOS, 124-3, BOTTOM_MENU_YPOS+6);
            lcd_lib_clear_string_centerP(BOTTOM_MENU_YPOS, bottom);
        }else{
            lcd_lib_draw_string_centerP(BOTTOM_MENU_YPOS, bottom);
        }
    }
}

void lcd_basic_screen()
{
    lcd_lib_clear();
    lcd_lib_draw_hline(3, 124, 51);
}

void lcd_info_screen(menuFunc_t cancelMenu, menuFunc_t callbackOnCancel, const char* cancelButtonText)
{
    if (lcd_lib_encoder_pos != ENCODER_NO_SELECTION)
    {
        if (lcd_lib_encoder_pos < 0)
            lcd_lib_encoder_pos += 2*ENCODER_TICKS_PER_MAIN_MENU_ITEM;
        if (lcd_lib_encoder_pos >= 2*ENCODER_TICKS_PER_MAIN_MENU_ITEM)
            lcd_lib_encoder_pos -= 2*ENCODER_TICKS_PER_MAIN_MENU_ITEM;
    }
    if (lcd_lib_button_pressed && IS_SELECTED_MAIN(0))
    {
        if (callbackOnCancel) callbackOnCancel();
        if (cancelMenu) menu.replace_menu(menu_t(cancelMenu));
    }

    lcd_basic_screen();

    if (!cancelButtonText) cancelButtonText = PSTR("CANCEL");
    if (IS_SELECTED_MAIN(0))
    {
        lcd_lib_draw_box(3+2, BOTTOM_MENU_YPOS-1, 124-2, BOTTOM_MENU_YPOS+7);
        lcd_lib_set(3+3, BOTTOM_MENU_YPOS, 124-3, BOTTOM_MENU_YPOS+6);
        lcd_lib_clear_stringP(65 - strlen_P(cancelButtonText) * 3, BOTTOM_MENU_YPOS, cancelButtonText);
    }else{
        lcd_lib_draw_stringP(65 - strlen_P(cancelButtonText) * 3, BOTTOM_MENU_YPOS, cancelButtonText);
    }
}

void lcd_question_screen(menuFunc_t optionAMenu, menuFunc_t callbackOnA, const char* AButtonText, menuFunc_t optionBMenu, menuFunc_t callbackOnB, const char* BButtonText)
{
    if (lcd_lib_encoder_pos != ENCODER_NO_SELECTION)
    {
        if (lcd_lib_encoder_pos < 0)
            lcd_lib_encoder_pos += 2*ENCODER_TICKS_PER_MAIN_MENU_ITEM;
        if (lcd_lib_encoder_pos >= 2*ENCODER_TICKS_PER_MAIN_MENU_ITEM)
            lcd_lib_encoder_pos -= 2*ENCODER_TICKS_PER_MAIN_MENU_ITEM;
    }
    if (lcd_lib_button_pressed)
    {
        if (IS_SELECTED_MAIN(0))
        {
            if (callbackOnA) callbackOnA();
            if (optionAMenu) menu.add_menu(menu_t(optionAMenu));
        }else if (IS_SELECTED_MAIN(1))
        {
            if (callbackOnB) callbackOnB();
            if (optionBMenu) menu.add_menu(menu_t(optionBMenu));
        }
    }

    lcd_basic_screen();

    if (IS_SELECTED_MAIN(0))
    {
        lcd_lib_draw_box(3+2, BOTTOM_MENU_YPOS-1, 64-2, BOTTOM_MENU_YPOS+7);
        lcd_lib_set(3+3, BOTTOM_MENU_YPOS, 64-3, BOTTOM_MENU_YPOS+6);
        lcd_lib_clear_stringP(35 - strlen_P(AButtonText) * 3, BOTTOM_MENU_YPOS, AButtonText);
    }else{
        lcd_lib_draw_stringP(35 - strlen_P(AButtonText) * 3, BOTTOM_MENU_YPOS, AButtonText);
    }
    if (IS_SELECTED_MAIN(1))
    {
        lcd_lib_draw_box(64+2, BOTTOM_MENU_YPOS-1, 64+60-2, BOTTOM_MENU_YPOS+7);
        lcd_lib_set(64+3, BOTTOM_MENU_YPOS, 64+60-3, BOTTOM_MENU_YPOS+6);
        lcd_lib_clear_stringP(64+31 - strlen_P(BButtonText) * 3, BOTTOM_MENU_YPOS, BButtonText);
    }else{
        lcd_lib_draw_stringP(64+31 - strlen_P(BButtonText) * 3, BOTTOM_MENU_YPOS, BButtonText);
    }
}

void lcd_progressbar(uint8_t progress)
{
    lcd_lib_draw_box(3, 39, 124, 47);

    for(uint8_t n=0; n<progress; ++n)
    {
        if (n>120) break;
        uint8_t m = (progress-n-1) % 12;
        if (m < 5)
            lcd_lib_draw_vline(4 + n, 41, 41+m);
        else if (m < 10)
            lcd_lib_draw_vline(4 + n, 41+m-5, 45);
    }
}

void lcd_cpyreturn(char * buffer)
{
    strcpy_P(buffer, PSTR("< RETURN"));
}

void lcd_draw_scroll_entry(uint8_t offsetY, char * buffer, uint8_t flags)
{
	uint8_t buffer_len = (uint8_t) strlen(buffer);
	char    backup     = '\0';
	uint8_t backup_pos = 0;
	if (flags & MENU_SELECTED)
	{
		if ((ui_mode & UI_SCROLL_ENTRY) && (buffer_len > LINE_ENTRY_TEXT_LENGHT))
		{
			line_entry_pos_update(LINE_ENTRY_MAX_STEP(buffer_len - LINE_ENTRY_TEXT_LENGHT));
			buffer    += LINE_ENTRY_TEXT_BEGIN();
			backup_pos = LINE_ENTRY_TEXT_LENGHT+LINE_ENTRY_TEXT_OFFSET();
			backup     = buffer[backup_pos];
			buffer[backup_pos] = '\0';
		}
		//
		lcd_lib_set(LCD_CHAR_MARGIN_LEFT-1, offsetY-1, LCD_GFX_WIDTH-LCD_CHAR_MARGIN_RIGHT, offsetY+7);
		lcd_lib_clear_string(LCD_CHAR_MARGIN_LEFT+LINE_ENTRY_GFX_BEGIN(), offsetY, buffer);
		//
		if (backup != '\0')
			buffer[backup_pos] = backup;
	}
	else
    {
		if ((ui_mode & UI_SCROLL_ENTRY) && (buffer_len > LINE_ENTRY_TEXT_LENGHT))
		{
			backup = buffer[LINE_ENTRY_TEXT_LENGHT];
			buffer[LINE_ENTRY_TEXT_LENGHT] = '\0';
		}
		//
		lcd_lib_draw_string(LCD_CHAR_MARGIN_LEFT, offsetY, buffer);
		//
		if (backup != '\0')
			buffer[LINE_ENTRY_TEXT_LENGHT] = backup;
	}
}

void lcd_scroll_menu(const char* menuNameP, int8_t entryCount, scrollDrawCallback_t entryDrawCallback, entryDetailsCallback_t entryDetailsCallback)
{
    if (lcd_lib_button_pressed)
		return;//Selection possibly changed the menu, so do not update it this cycle.

    if (lcd_lib_encoder_pos == ENCODER_NO_SELECTION)
        lcd_lib_encoder_pos = 0;

	static int16_t viewPos = 0;
	if (lcd_lib_encoder_pos < 0) lcd_lib_encoder_pos += entryCount * ENCODER_TICKS_PER_SCROLL_MENU_ITEM;
	if (lcd_lib_encoder_pos >= entryCount * ENCODER_TICKS_PER_SCROLL_MENU_ITEM) lcd_lib_encoder_pos -= entryCount * ENCODER_TICKS_PER_SCROLL_MENU_ITEM;

    uint8_t selIndex = uint16_t(lcd_lib_encoder_pos/ENCODER_TICKS_PER_SCROLL_MENU_ITEM);

    lcd_lib_clear();

    int16_t targetViewPos = (selIndex << 3) - 15;

    int16_t viewDiff = targetViewPos - viewPos;
    viewPos += viewDiff / 4;
    if      (viewDiff > 0) { ++viewPos; line_entry_pos_reset(); }
    else if (viewDiff < 0) { --viewPos; line_entry_pos_reset(); }

    uint8_t drawOffset = 11 - (uint16_t(viewPos) & 0x07);
    uint8_t itemOffset = uint16_t(viewPos) >> 3;
    for(uint8_t n=0; n<6; n++)
    {
        uint8_t itemIdx = n + itemOffset;
        if (itemIdx >= entryCount)
            continue;

        entryDrawCallback(itemIdx, drawOffset+8*n, (itemIdx == selIndex) ? MENU_SELECTED : 0);

    }
    lcd_lib_set(3, 0, 124, 8);
    lcd_lib_clear(3, 49, 124, 63);
    lcd_lib_clear(3, 9, 124, 9);

    lcd_lib_draw_hline(3, 124, 50);

    lcd_lib_clear_string_centerP(1, menuNameP);

    if (entryDetailsCallback)
    {
        entryDetailsCallback(selIndex);
    }
}

void lcd_menu_edit_setting()
{
    if (lcd_lib_encoder_pos < lcd_setting_min)
        lcd_lib_encoder_pos = lcd_setting_min;
    if (lcd_lib_encoder_pos > lcd_setting_max)
        lcd_lib_encoder_pos = lcd_setting_max;

    if (lcd_setting_type == 1)
        *(uint8_t*)lcd_setting_ptr = lcd_lib_encoder_pos;
    else if (lcd_setting_type == 2)
        *(uint16_t*)lcd_setting_ptr = lcd_lib_encoder_pos;
    else if (lcd_setting_type == 3)
        *(float*)lcd_setting_ptr = float(lcd_lib_encoder_pos) / 100.0;
    else if (lcd_setting_type == 4)
        *(int32_t*)lcd_setting_ptr = lcd_lib_encoder_pos;
    else if (lcd_setting_type == 5)
        *(uint8_t*)lcd_setting_ptr = lcd_lib_encoder_pos * 255 / 100;
    else if (lcd_setting_type == 6)
        *(float*)lcd_setting_ptr = float(lcd_lib_encoder_pos) * 60;
    else if (lcd_setting_type == 7)
        *(float*)lcd_setting_ptr = float(lcd_lib_encoder_pos) * 100;
    else if (lcd_setting_type == 8)
        *(float*)lcd_setting_ptr = float(lcd_lib_encoder_pos);

    lcd_basic_screen();
    lcd_lib_draw_string_centerP(20, lcd_setting_name);
    char buffer[20] = {0};
    if (lcd_setting_type == 3)
        float_to_string2(float(lcd_lib_encoder_pos) / 100.0, buffer, lcd_setting_postfix);
    else
        int_to_string(lcd_lib_encoder_pos, buffer, lcd_setting_postfix);
    lcd_lib_draw_string_center(30, buffer);

    strcpy_P(buffer, PSTR("Prev: "));
    if (lcd_setting_type == 3)
        float_to_string2(float(lcd_setting_start_value) / 100.0, buffer + 6, lcd_setting_postfix);
    else
        int_to_string(lcd_setting_start_value, buffer + 6, lcd_setting_postfix);
    lcd_lib_draw_string_center(BOTTOM_MENU_YPOS, buffer);

    if (lcd_lib_button_pressed)
        menu.return_to_previous();

    lcd_lib_update_screen();
}

static void lcd_menu_material_reheat()
{
    last_user_interaction = millis();
    int16_t temp = 0;
    int16_t target = 0;
    for (uint8_t e=0; e<EXTRUDERS; ++e)
    {
        int16_t t = degHotend(e);
        if (t < 0) t = 0;
        temp += t;
        t = degTargetHotend(e) - 5;
        target += t;
    }
    if (temp > target)
    {
        menu.return_to_previous(false);
    }

    uint8_t progress = uint8_t(temp * 125 / target);
    if (progress < minProgress)
        progress = minProgress;
    else
        minProgress = progress;

    lcd_lib_clear();
    lcd_lib_draw_string_centerP(10, PSTR("Heating printhead"));

    char buffer[32] = {0};
    char *c = buffer;
    for(uint8_t e=0; e<EXTRUDERS; ++e)
    {
        c = int_to_string(dsp_temperature[e], c, PSTR("C/"));
        c = int_to_string(degTargetHotend(e), c, PSTR("C "));
    }
    --c;
    *c = '\0';

    lcd_lib_draw_string_center(24, buffer);
    // lcd_lib_draw_heater(LCD_GFX_WIDTH/2-2, 40, getHeaterPower(active_extruder));

    lcd_progressbar(progress);

    lcd_lib_update_screen();
}

bool check_heater_timeout()
{
    if (heater_timeout && !commands_queued() && !HAS_SERIAL_CMD)
    {
        const unsigned long timeout = last_user_interaction + (heater_timeout * (unsigned long)MILLISECONDS_PER_MINUTE);
        if (timeout < millis())
        {
            for(uint8_t e=0; e<EXTRUDERS; ++e)
            {
                if (target_temperature[e] > (EXTRUDE_MINTEMP - 40))
                {
                    backup_temperature[e] = target_temperature[e];
                    // switch off nozzle heater
                    setTargetHotend(0, e);
                }
            }
            return false;
        }
    }
    return true;
}

bool check_preheat(uint8_t e)
{
    int16_t target = degTargetHotend(e);
    if (!target)
    {
        setTargetHotend(backup_temperature[e], e);
        minProgress = 0;
        if (menu.currentMenu().processMenuFunc != lcd_menu_material_reheat)
        {
            menu.add_menu(menu_t(lcd_menu_material_reheat));
        }
        return false;
    }
    return true;
}


#endif//ENABLE_ULTILCD2
