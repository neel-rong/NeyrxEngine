#include "Neyrx/Core/Logger.h"
#include <iostream>
#include<conio.h>

int main()
{
#ifdef NEYRX_ENABLE_LOGGING
	std::cout << "Logging Enabled" << std::endl;
#else
	std::cout << "Logging Disabled" << std::endl;
#endif

#ifdef NEYRX_ENABLE_DEVELOPMENT
	std::cout << "Development Enabled" << std::endl;
#else
	std::cout << "Development Dsiabled" << std::endl;
#endif
	getch();

	return 0;
}