/*
 * OpenDispense 2 
 * UCC (University [of WA] Computer Club) Electronic Accounting System
 * - Dispense Client
 *
 * menu.c - ncurses dispense menu
 *
 * This file is licenced under the 3-clause BSD Licence. See the file
 * COPYING for full details.
 */
#include <stdlib.h>
#include <ncurses.h>
#include <pwd.h>	// getpwuids
#include <unistd.h>	// getuid
#include "common.h"

// === CONSTANTS ===
#define COLOURPAIR_DEFAULT	0
#define COLOURPAIR_CANTBUY	1
#define COLOURPAIR_SELECTED	2

// === PROTOTYPES ===
 int	ShowItemAt(int Row, int Col, int Width, int Index, int bHilighted);
void	PrintAlign(int Row, int Col, int Width, const char *Left, char Pad1, const char *Mid, char Pad2, const char *Right, ...);

// -------------------
// --- NCurses GUI ---
// -------------------
/**
 * \brief Render the NCurses UI
 */
int ShowNCursesUI(void)
{
	 int	ch;
	 int	i, times;
	 int	xBase, yBase;
	const int	displayMinWidth = 50;
	const char	*titleString = "Dispense";
	 int	items_in_view;
	 int	maxItemIndex;
	 int	itemBase = 0;
	 int	currentItem;
	 int	ret = -2;	// -2: Used for marking "no return yet"
	
	char	balance_str[5+1+2+1];	// If $9999.99 is too little, something's wrong
	char	*username;
	struct passwd *pwd;
	 
	 int	height, width;
	
	void _ItemDown(void)
	{
		currentItem ++;
		// Skip over spacers
		while( ShowItemAt(0, 0, 0, currentItem, 0) == -1 )
			currentItem ++;
		
		if( currentItem >= maxItemIndex ) {
			currentItem = 0;
			// Skip over spacers
			while( ShowItemAt(0, 0, 0, currentItem, 0) == -1 )
				currentItem ++;
		}
	}
	
	void _ItemUp(void)
	{
		currentItem --;
		// Skip over spacers
		while( ShowItemAt(0, 0, 0, currentItem, 0) == -1 )
			currentItem --;
		
		if( currentItem < 0 ) {
			currentItem = maxItemIndex - 1;
			// Skip over spacers
			while( ShowItemAt(0, 0, 0, currentItem, 0) == -1 )
				currentItem --;
		}
	}

	// Get Username
	if( gsEffectiveUser )
		username = gsEffectiveUser;
	else {
		pwd = getpwuid( getuid() );
		username = pwd->pw_name;
	}
	// Get balance
	snprintf(balance_str, sizeof(balance_str), "$%i.%02i", giUserBalance/100, abs(giUserBalance)%100);
	
	// Enter curses mode
	initscr();
	start_color();
	use_default_colors();
	init_pair(COLOURPAIR_DEFAULT,  -1,  -1);	// Not avaliable
	init_pair(COLOURPAIR_CANTBUY,  COLOR_BLACK,  -1);	// Not avaliable
	init_pair(COLOURPAIR_SELECTED, COLOR_GREEN,  -1);	// Selected
	cbreak(); noecho();
	
	// Get max index
	maxItemIndex = ShowItemAt(0, 0, 0, -1, 0);
	// Get item count per screen
	// - 6: randomly chosen (Need at least 3)
	items_in_view = LINES - 6;
	if( items_in_view > maxItemIndex )
		items_in_view = maxItemIndex;
	// Get first index
	currentItem = 0;
	while( ShowItemAt(0, 0, 0, currentItem, 0) == -1 )
		currentItem ++;
	
	
	// Get dimensions
	height = items_in_view + 3;
	width = displayMinWidth;
	
	// Get positions
	xBase = COLS/2 - width/2;
	yBase = LINES/2 - height/2;
	
	for( ;; )
	{
		// Header
		PrintAlign(yBase, xBase, width, "/", '-', titleString, '-', "\\");
		
		// Items
		for( i = 0; i < items_in_view; i ++ )
		{
			 int	pos = 0;
			
			move( yBase + 1 + i, xBase );
			printw("| ");
			
			pos += 2;
			
			// Check for the '...' row
			// - Oh god, magic numbers!
			if( (i == 0 && itemBase > 0)
			 || (i == items_in_view - 1 && itemBase < maxItemIndex - items_in_view) )
			{
				printw("     ...");	pos += 8;
				times = (width - pos) - 1;
				while(times--)	addch(' ');
			}
			// Show an item
			else {
				ShowItemAt(
					yBase + 1 + i, xBase + pos,	// Position
					(width - pos) - 3,	// Width
					itemBase + i,	// Index
					!!(currentItem == itemBase + i)	// Hilighted
					);
				printw("  ");
			}
			
			// Scrollbar (if needed)
			if( maxItemIndex > items_in_view ) {
				if( i == 0 ) {
					addch('A');
				}
				else if( i == items_in_view - 1 ) {
					addch('V');
				}
				else {
					 int	percentage = itemBase * 100 / (maxItemIndex-items_in_view);
					if( i-1 == percentage*(items_in_view-3)/100 ) {
						addch('#');
					}
					else {
						addch('|');
					}
				}
			}
			else {
				addch('|');
			}
		}
		
		// Footer
		PrintAlign(yBase+height-2, xBase, width, "\\", '-', "", '-', "/");
		
		// User line
		// - Username, balance, flags
		PrintAlign(yBase+height-1, xBase+1, width-2,
			username, ' ', balance_str, ' ', gsUserFlags);
		PrintAlign(yBase+height, xBase+1, width-2,
			"q: Quit", ' ', "Arrows: Select", ' ', "Enter: Buy");
		
		
		// Get input
		ch = getch();
		
		if( ch == '\x1B' ) {
			ch = getch();
			if( ch == '[' ) {
				ch = getch();
				
				switch(ch)
				{
				case 'B':	_ItemDown();	break;
				case 'A':	_ItemUp();	break;
				}
			}
			else if( ch == ERR || ch == '\x1b' ) {
				ret = -1;
				break;
			}
			else {
				fprintf(stderr, "Unknown character 0x%x\n", ch);
			}
		}
		else {
			switch(ch)
			{
			case '\n':
				ret = ShowItemAt(0, 0, 0, currentItem, 0);
				break;
			case 'h':	break;
			case 'j':	_ItemDown();	break;
			case 'k':	_ItemUp();	break;
			case 'l':	break;
			case 'q':
				ret = -1;	// -1: Return with no dispense
				break;
			}
			
			// Check if the return value was changed
			if( ret != -2 )	break;
		}
		
		// Scroll only if needed
		if( items_in_view < maxItemIndex )
		{
			// - If the current item is above the second item shown, and we're not at the top
			if( currentItem < itemBase + 2 && itemBase > 0 ) {
				itemBase = currentItem - 2;
				if(itemBase < 0)	itemBase = 0;
			}
			// - If the current item is below the second item show, and we're not at the bottom
			if( currentItem > itemBase + items_in_view - 2 && itemBase + items_in_view < maxItemIndex ) {
				itemBase = currentItem - items_in_view + 2;
				if( itemBase > maxItemIndex - items_in_view )
					itemBase = maxItemIndex - items_in_view;
			}
		}
	}
	
	
	// Leave
	endwin();
	return ret;
}

/**
 * \brief Show item \a Index at (\a Col, \a Row)
 * \return Dispense index of item
 * \note Part of the NCurses UI
 */
int ShowItemAt(int Row, int Col, int Width, int Index, int bHilighted)
{
	char	*name = NULL;
	 int	price = 0;
	 int	status = -1;
	
	switch(giUIMode)
	{
	// Standard UI
	// - This assumes that 
	case UI_MODE_STANDARD:
		// Bounds check
		// Index = -1, request limit
		if( Index < 0 || Index >= giNumItems+2 )
			return giNumItems+2;
		// Drink label
		if( Index == 0 )
		{
			price = 0;
			name = "Coke Machine";
			Index = -1;	// -1 indicates a label
			break;
		}
		Index --;
		// Drinks 0 - 6
		if( Index <= 6 )
		{
			name = gaItems[Index].Desc;
			price = gaItems[Index].Price;
			status = gaItems[Index].Status;
			break;
		}
		Index -= 7;
		// EPS label
		if( Index == 0 )
		{
			price = 0;
			name = "Electronic Payment System";
			Index = -1;	// -1 indicates a label
			break;
		}
		Index --;
		Index += 7;
		name = gaItems[Index].Desc;
		price = gaItems[Index].Price;
		status = gaItems[Index].Status;
		break;
	default:
		return -1;
	}
	
	// Width = 0, don't print
	if( Width > 0 )
	{
		// 4 preceding, 5 price
		int nameWidth = Width - 4 - snprintf(NULL, 0, " %4i", price);
		move( Row, Col );
		
		if( Index >= 0 )
		{
			// Show hilight and status
			switch( status )
			{
			case 0:
				if( bHilighted ) {
					color_set( COLOURPAIR_SELECTED, NULL );
					printw("->  ");
				}
				else if( price > giUserBalance ) {
					attrset(A_BOLD);
					color_set( COLOURPAIR_CANTBUY, NULL );
					printw("    ");
				}
				else {
					color_set( 0, NULL );
					printw("    ");
				}
				break;
			case 1:
				attrset(A_BOLD);
				color_set( COLOURPAIR_CANTBUY, NULL );
				printw("SLD ");
				break;
			
			default:
			case -1:
				color_set( COLOURPAIR_CANTBUY, NULL );
				printw("ERR ");
				break;
			}
			
			printw("%-*.*s", nameWidth, nameWidth, name);
		
			printw(" %4i", price);
			color_set(0, NULL);
			attrset(A_NORMAL);
		}
		else
		{
			printw("-- %-*.*s ", Width-4, Width-4, name);
		}
	}
	
	// If the item isn't availiable for sale, return -1 (so it's skipped)
	if( status > 0 || (price > giUserBalance && gbDisallowSelectWithoutBalance) )
		Index = -2;
	
	return Index;
}

/**
 * \brief Print a three-part string at the specified position (formatted)
 * \note NCurses UI Helper
 * 
 * Prints \a Left on the left of the area, \a Right on the righthand side
 * and \a Mid in the middle of the area. These are padded with \a Pad1
 * between \a Left and \a Mid, and \a Pad2 between \a Mid and \a Right.
 * 
 * ::printf style format codes are allowed in \a Left, \a Mid and \a Right,
 * and the arguments to these are read in that order.
 */
void PrintAlign(int Row, int Col, int Width, const char *Left, char Pad1,
	const char *Mid, char Pad2, const char *Right, ...)
{
	 int	lLen, mLen, rLen;
	 int	times;
	
	va_list	args;
	
	// Get the length of the strings
	va_start(args, Right);
	lLen = vsnprintf(NULL, 0, Left, args);
	mLen = vsnprintf(NULL, 0, Mid, args);
	rLen = vsnprintf(NULL, 0, Right, args);
	va_end(args);
	
	// Sanity check
	if( lLen + mLen/2 > Width/2 || mLen/2 + rLen > Width/2 ) {
		return ;	// TODO: What to do?
	}
	
	move(Row, Col);
	
	// Render strings
	va_start(args, Right);
	// - Left
	{
		char	tmp[lLen+1];
		vsnprintf(tmp, lLen+1, Left, args);
		addstr(tmp);
	}
	// - Left padding
	times = (Width - mLen)/2 - lLen;
	while(times--)	addch(Pad1);
	// - Middle
	{
		char	tmp[mLen+1];
		vsnprintf(tmp, mLen+1, Mid, args);
		addstr(tmp);
	}
	// - Right Padding
	times = (Width - mLen)/2 - rLen;
	if( (Width - mLen) % 2 )	times ++;
	while(times--)	addch(Pad2);
	// - Right
	{
		char	tmp[rLen+1];
		vsnprintf(tmp, rLen+1, Right, args);
		addstr(tmp);
	}
}

