// license:BSD-3-Clause
/***************************************************************************

    plusmenus.h

    Internal UI menus ported from MAMEPlus: autofire settings,
    custom button combinations and scale effect selection.

***************************************************************************/

#ifndef MAME_FRONTEND_MAME_UI_PLUSMENUS_H
#define MAME_FRONTEND_MAME_UI_PLUSMENUS_H

#pragma once

#include "ui/menu.h"

namespace ui {

class menu_autofire : public menu
{
public:
	menu_autofire(mame_ui_manager &mui, render_container &container);
	virtual ~menu_autofire();

protected:
	virtual void populate() override;
	virtual bool handle(event const *ev) override;

private:
	enum { AUTOFIRE_ITEM_P1_DELAY = 1 };
};


class menu_custom_button : public menu
{
public:
	menu_custom_button(mame_ui_manager &mui, render_container &container);
	virtual ~menu_custom_button();

protected:
	virtual void populate() override;
	virtual bool handle(event const *ev) override;
};


class menu_scale_effect : public menu
{
public:
	menu_scale_effect(mame_ui_manager &mui, render_container &container);
	virtual ~menu_scale_effect();

protected:
	virtual void populate() override;
	virtual bool handle(event const *ev) override;
};

} // namespace ui

#endif // MAME_FRONTEND_MAME_UI_PLUSMENUS_H
