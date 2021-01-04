/* Reverse Engineer's Hex Editor
 * Copyright (C) 2017-2020 Daniel Collins <solemnwarning@solemnwarning.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include "platform.hpp"

#include "app.hpp"
#include "ArtProvider.hpp"
#include "Events.hpp"
#include "mainwindow.hpp"
#include "Palette.hpp"

/* These MUST come after any wxWidgets headers. */
#ifdef _WIN32
#include <objbase.h>
#endif

IMPLEMENT_APP(REHex::App);

bool REHex::App::OnInit()
{
	call_setup_hooks(SetupPhase::EARLY);
	
	#ifdef _WIN32
	/* Needed for shell API calls. */
	CoInitialize(NULL);
	#endif
	
	wxImage::AddHandler(new wxPNGHandler);
	
	ArtProvider::init();
	
	config = new wxConfig("REHex");
	
	config->SetPath("/");
	last_directory = config->Read("last-directory", "");
	font_size_adjustment = config->ReadLong("font-size-adjustment", 0);
	
	/* Display default tool panels if a default view hasn't been configured. */
	if(!config->HasGroup("/default-view/"))
	{
		config->SetPath("/default-view/vtools/panels/0/tab/0");
		config->Write("name", "DecodePanel");
		config->Write("selected", true);
		config->Write("big-endian", false);
		
		config->SetPath("/default-view/vtools/panels/0/tab/1");
		config->Write("name", "CommentTree");
		config->Write("selected", false);
	}
	
	recent_files = new wxFileHistory();
	
	config->SetPath("/recent-files/");
	recent_files->Load(*config);
	
	config->SetPath("/");
	
	std::string theme = config->Read("theme", "system").ToStdString();
	if(theme == "light")
	{
		active_palette = Palette::create_light_palette();
	}
	else if(theme == "dark")
	{
		active_palette = Palette::create_dark_palette();
	}
	else /* if(theme == "system") */
	{
		active_palette = Palette::create_system_palette();
	}
	
	call_setup_hooks(SetupPhase::READY);
	
	wxSize windowSize(740, 540);
	
	#ifndef __APPLE__
	config->Read("/default-view/window-width", &windowSize.x, windowSize.x);
	config->Read("/default-view/window-height", &windowSize.y, windowSize.y);
	#endif
	
	REHex::MainWindow *window = new REHex::MainWindow(windowSize);
	
	#ifndef __APPLE__
	bool maximise = config->ReadBool("/default-view/window-maximised", false);
	window->Maximize(maximise);
	#endif
	
	window->Show(true);
	
	if(argc > 1)
	{
		for(int i = 1; i < argc; ++i)
		{
			window->open_file(argv[i].ToStdString());
		}
	}
	else{
		window->new_file();
	}
	
	call_setup_hooks(SetupPhase::DONE);
	
	return true;
}

int REHex::App::OnExit()
{
	config->SetPath("/recent-files/");
	recent_files->Save(*config);
	
	config->SetPath("/");
	config->Write("last-directory", wxString(last_directory));
	
	delete active_palette;
	delete recent_files;
	delete config;
	
	#ifdef _WIN32
	CoUninitialize();
	#endif
	
	return 0;
}

const std::string &REHex::App::get_last_directory()
{
	return last_directory;
}

void REHex::App::set_last_directory(const std::string &last_directory)
{
	this->last_directory = last_directory;
}

int REHex::App::get_font_size_adjustment() const
{
	return font_size_adjustment;
}

void REHex::App::set_font_size_adjustment(int font_size_adjustment)
{
	this->font_size_adjustment = font_size_adjustment;
	
	FontSizeAdjustmentEvent event(font_size_adjustment);
	ProcessEvent(event);
}

std::vector<std::string> REHex::App::get_plugin_directories()
{
	std::vector<std::string> plugin_directories;
	
	plugin_directories.push_back("./plugins/");
	
	/* TODO: Sensible plugins paths
	 *
	 * Linux
	 *   ~/.rehex/plugins/ OR ${XDG_DATA_HOME}/rehex/plugins/ ?
	 *   <libdir>/rehex/
	 *
	 * Windows
	 *   <exe dir>/Plugins/
	 *   <Local Settings>/Application Data/REHex/Plugins/
	 *
	 * Mac OS
	 *   <Bundle>/Contents/PlugIns/
	 *
	 * Also want plugins relative to binary when doing dev on Linux...
	*/
	
	return plugin_directories;
}

std::multimap<REHex::App::SetupPhase, const REHex::App::SetupHookFunction*> *REHex::App::setup_hooks = NULL;

void REHex::App::register_setup_hook(SetupPhase phase, const SetupHookFunction *func)
{
	if(setup_hooks == NULL)
	{
		setup_hooks = new std::multimap<SetupPhase, const SetupHookFunction*>;
	}
	
	setup_hooks->insert(std::make_pair(phase, func));
}

void REHex::App::unregister_setup_hook(SetupPhase phase, const SetupHookFunction *func)
{
	auto i = std::find_if(
		setup_hooks->begin(), setup_hooks->end(),
		[&](const std::pair<SetupPhase, const SetupHookFunction*> &elem) { return elem.first == phase && elem.second == func; });
	
	setup_hooks->erase(i);
	
	if(setup_hooks->empty())
	{
		delete setup_hooks;
		setup_hooks = NULL;
	}
}

void REHex::App::call_setup_hooks(SetupPhase phase)
{
	if(setup_hooks == NULL)
	{
		/* No hooks registered. */
		return;
	}
	
	for(auto i = setup_hooks->begin(); i != setup_hooks->end(); ++i)
	{
		if(i->first == phase)
		{
			const SetupHookFunction &func = *(i->second);
			func();
		}
	}
}

REHex::App::SetupHookRegistration::SetupHookRegistration(SetupPhase phase, const SetupHookFunction &func):
	phase(phase),
	func(func)
{
	App::register_setup_hook(phase, &(this->func));
}

REHex::App::SetupHookRegistration::~SetupHookRegistration()
{
	App::unregister_setup_hook(phase, &func);
}
