/*  Copyright 2005 Guillaume Duhamel
	Copyright 2005-2006 Theo Berkau
	Copyright 2008 Filipe Azevedo <pasnox@gmail.com>

	This file is part of Yabause.

	Yabause is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	Yabause is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Yabause; if not, write to the Free Software
	Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
*/
#include <QApplication>

#include "QtYabause.h"
#include "Settings.h"
#include "ui/UIYabause.h"
#ifndef NO_CLI
#include "Arguments.h"
#endif

int main( int argc, char** argv )
{
#ifdef _WIN32
	//attach the console if the program was started from the console (parent) so that we get console output.
	auto stdout_type = GetFileType(GetStdHandle(STD_OUTPUT_HANDLE));
	auto stderr_type = GetFileType(GetStdHandle(STD_ERROR_HANDLE));
	if (AttachConsole(ATTACH_PARENT_PROCESS)) {
		if (stdout_type == FILE_TYPE_UNKNOWN)
		{
			freopen("CONOUT$", "w", stdout);
		}
		if (stderr_type == FILE_TYPE_UNKNOWN)
		{
			freopen("CONOUT$", "w", stderr);
		}
		//freopen("CONIN$", "r", stdin);
	}
#endif

	// create application
	//QCoreApplication::addLibraryPath(".");
	//QCoreApplication::addLibraryPath(<result of GetModuleHandleExA>);
	QApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
	QApplication app( argc, argv );
	// init application
	app.setApplicationName( QString( "Kronos v%1" ).arg( VERSION ) );
	// init settings
	Settings::setIniInformations();
#ifdef HAVE_LIBMINI18N
	// set translation file
	if ( QtYabause::setTranslationFile() == -1 )
		qWarning( "Can't set translation file" );
#endif
#ifndef NO_CLI
	Arguments::parse();
#endif
	// show main window
	QtYabause::mainWindow()->setWindowTitle( app.applicationName() );
	QtYabause::mainWindow()->show();
	// connection
	QObject::connect( &app, SIGNAL( lastWindowClosed() ), &app, SLOT( quit() ) );
	// exec application
	int i = app.exec();
	QtYabause::closeTranslation();
	return i;
}
