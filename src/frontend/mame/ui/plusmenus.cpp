// license:BSD-3-Clause
/***************************************************************************

    plusmenus.cpp

    Internal UI menus ported from MAMEPlus: autofire settings,
    custom button combinations and scale effect selection.

***************************************************************************/

#include "emu.h"
#include "plusmenus.h"

#include "emuopts.h"
#include "input.h"
#include "ioport.h"
#include "screen.h"

#include "scale/osdscale.h"

#include <string>


namespace ui {

/***************************************************************************
    AUTOFIRE SETTINGS MENU
***************************************************************************/

menu_autofire::menu_autofire(mame_ui_manager &mui, render_container &container)
	: menu(mui, container)
{
	set_heading(_("autofire", "Autofire Settings"));
}

menu_autofire::~menu_autofire()
{
}


//-------------------------------------------------
//  populate
//-------------------------------------------------

void menu_autofire::populate()
{
	int players = 0;

	// iterate over the input ports and add autofire toggle items
	for (auto &port : machine().ioport().ports())
	{
		for (ioport_field &field : port.second->fields())
		{
			std::string const name(field.name());

			if (!name.empty() &&
				((field.type() >= IPT_BUTTON1 && field.type() < IPT_BUTTON1 + MAX_NORMAL_BUTTONS) ||
				 (field.type() >= IPT_CUSTOM1 && field.type() < IPT_CUSTOM1 + MAX_CUSTOM_BUTTONS)))
			{
				if (players < field.player() + 1)
					players = field.player() + 1;

				char const *subtext;
				switch (field.live().autofire)
				{
					case AUTOFIRE_ON:       subtext = _("autofire", "On");     break;
					case AUTOFIRE_TOGGLE:   subtext = _("autofire", "Toggle"); break;
					default:                subtext = _("autofire", "Off");    break;
				}
				item_append(name, subtext, FLAG_LEFT_ARROW | FLAG_RIGHT_ARROW, (void *)&field.live().autofire);
			}
		}
	}

	// add per-player autofire delay items
	for (int i = 0; i < players; i++)
	{
		item_append(util::string_format(_("autofire", "P%1$u %2$s"), i + 1, _("autofire", "Autofire Delay")),
					util::string_format("%d", machine().ioport().get_autofiredelay(i)),
					FLAG_LEFT_ARROW | FLAG_RIGHT_ARROW,
					(void *)(uintptr_t(i + AUTOFIRE_ITEM_P1_DELAY)));
	}
}


//-------------------------------------------------
//  handle
//-------------------------------------------------

bool menu_autofire::handle(event const *ev)
{
	bool changed = false;

	// handle events
	if (ev && ev->itemref)
	{
		if ((ev->iptkey == IPT_UI_LEFT) || (ev->iptkey == IPT_UI_RIGHT))
		{
			int const player = int(uintptr_t(ev->itemref)) - AUTOFIRE_ITEM_P1_DELAY;

			if ((player >= 0) && (player < MAX_PLAYERS))
			{
				// autofire delay
				int autofire_delay = machine().ioport().get_autofiredelay(player);

				if (ev->iptkey == IPT_UI_LEFT)
				{
					if (--autofire_delay < 1)
						autofire_delay = 1;
				}
				else
				{
					if (++autofire_delay > 99)
						autofire_delay = 99;
				}

				machine().ioport().set_autofiredelay(player, autofire_delay);
				ev->item->set_subtext(util::string_format("%d", autofire_delay));
				changed = true;
			}
			else
			{
				// anything else is a toggle item
				int *const selected = (int *)ev->itemref;
				int selected_value = *selected;

				if (ev->iptkey == IPT_UI_LEFT)
				{
					if (--selected_value < 0)
						selected_value = 2;
				}
				else
				{
					if (++selected_value > 2)
						selected_value = 0;
				}

				*selected = selected_value;

				char const *subtext;
				switch (*selected)
				{
					case AUTOFIRE_ON:       subtext = _("autofire", "On");     break;
					case AUTOFIRE_TOGGLE:   subtext = _("autofire", "Toggle"); break;
					default:                subtext = _("autofire", "Off");    break;
				}
				ev->item->set_subtext(subtext);
				changed = true;
			}
		}
	}

	return changed;
}


/***************************************************************************
    CUSTOM BUTTON MENU
***************************************************************************/

menu_custom_button::menu_custom_button(mame_ui_manager &mui, render_container &container)
	: menu(mui, container)
{
	set_heading(_("autofire", "Custom Buttons"));
}

menu_custom_button::~menu_custom_button()
{
}


//-------------------------------------------------
//  populate
//-------------------------------------------------

void menu_custom_button::populate()
{
	// loop over the input ports and add the custom button items
	for (auto &port : machine().ioport().ports())
	{
		for (ioport_field &field : port.second->fields())
		{
			int const player = field.player();
			int const type = field.type();

			if ((type >= IPT_CUSTOM1) && (type < IPT_CUSTOM1 + MAX_CUSTOM_BUTTONS))
			{
				int const which = type - IPT_CUSTOM1;
				std::string subtext;
				int n = 1;

				// unpack the custom button combination
				for (int i = 0; i < MAX_NORMAL_BUTTONS; i++, n <<= 1)
					if (machine().ioport().get_custom_button(player, which) & n)
					{
						if (!subtext.empty())
							subtext.append("+");
						subtext.append(1, char('A' + i));
					}

				// the ref points at this custom button field
				item_append(field.name(), subtext, FLAG_LEFT_ARROW | FLAG_RIGHT_ARROW, &field);
			}
		}
	}

	item_append(menu_item_type::SEPARATOR, 0);
	item_append(_("autofire", "Select a combination, then press 1-9/0 to toggle buttons"), "", FLAG_DISABLE, nullptr);
}


//-------------------------------------------------
//  handle
//-------------------------------------------------

bool menu_custom_button::handle(event const *ev)
{
	bool changed = false;

	if (ev && ev->itemref)
	{
		// find which custom button the selected item belongs to
		ioport_field *const field = (ioport_field *)ev->itemref;
		int const player = field->player();
		int const which = field->type() - IPT_CUSTOM1;

		// count the number of action buttons actually in use
		int custom_buttons_count = 0;
		for (auto &port : machine().ioport().ports())
		{
			for (ioport_field &f : port.second->fields())
			{
				if ((f.type() >= IPT_BUTTON1) && (f.type() < IPT_BUTTON1 + MAX_NORMAL_BUTTONS))
				{
					int const num = f.type() - IPT_BUTTON1;
					if (num >= custom_buttons_count)
						custom_buttons_count = num + 1;
				}
			}
		}
		if (custom_buttons_count > 10)
			custom_buttons_count = 10;

		// number keys toggle the corresponding button in/out of the combination
		input_item_id id = ITEM_ID_1;
		for (int i = 0; i < custom_buttons_count; i++, id++)
		{
			if (i == 9)
				id = ITEM_ID_0;

			if (machine().input().code_pressed_once(input_code(DEVICE_CLASS_KEYBOARD, 0, ITEM_CLASS_SWITCH, ITEM_MODIFIER_NONE, id)))
			{
				u16 const oldval = machine().ioport().get_custom_button(player, which);
				u16 const newval = oldval ^ u16(1 << i);
				machine().ioport().set_custom_button(player, which, newval);

				// refresh the subtext of the item we belong to
				std::string subtext;
				int n = 1;
				for (int j = 0; j < MAX_NORMAL_BUTTONS; j++, n <<= 1)
					if (newval & n)
					{
						if (!subtext.empty())
							subtext.append("+");
						subtext.append(1, char('A' + j));
					}
				ev->item->set_subtext(subtext);

				changed = true;
				break;
			}
		}
	}

	return changed;
}


/***************************************************************************
    SCALE EFFECT MENU
***************************************************************************/

menu_scale_effect::menu_scale_effect(mame_ui_manager &mui, render_container &container)
	: menu(mui, container)
{
	set_heading(_("autofire", "Image Enhancement"));
}

menu_scale_effect::~menu_scale_effect()
{
}


//-------------------------------------------------
//  populate
//-------------------------------------------------

void menu_scale_effect::populate()
{
	char const *const current = machine().options().scale_effect();

	for (int i = 0; i < scale_count(); i++)
	{
		bool const is_current = !strcmp(current, scale_name(i));
		item_append(_("autofire", scale_desc(i)),
					is_current ? _("autofire", "Current") : "",
					FLAG_LEFT_ARROW | FLAG_RIGHT_ARROW,
					(void *)(uintptr_t(i)));
	}
}


//-------------------------------------------------
//  handle
//-------------------------------------------------

bool menu_scale_effect::handle(event const *ev)
{
	if (ev && ev->itemref)
	{
		int const selected = int(uintptr_t(ev->itemref));

		if ((ev->iptkey == IPT_UI_SELECT) || (ev->iptkey == IPT_UI_LEFT) || (ev->iptkey == IPT_UI_RIGHT))
		{
			// switch to the selected effect (left/right move one entry)
			int new_effect = selected;
			if (ev->iptkey == IPT_UI_LEFT)
				new_effect = (selected + scale_count() - 1) % scale_count();
			else if (ev->iptkey == IPT_UI_RIGHT)
				new_effect = (selected + 1) % scale_count();

			machine().options().set_value(OPTION_SCALE_EFFECT, scale_name(new_effect), OPTION_PRIORITY_CMDLINE);

			// reinitialize every screen with the new effect
			for (screen_device &screen : screen_device_enumerator(machine().root_device()))
				screen.reinit_scale_effect();

			reset(reset_options::REMEMBER_REF);
			return true;
		}
	}

	return false;
}

} // namespace ui
