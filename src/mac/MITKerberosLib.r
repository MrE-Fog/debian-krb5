#include "Balloons.r"

#include "version.r"

#include "MIT Kerberos.r"

type 'CCI�' as 'STR ';

resource 'hfdr' (-5696, purgeable) {
	HelpMgrVersion,
	hmDefaultOptions,
	0,
	0,
	{	/* array HFdrArray: 1 elements */
		/* [1] */
		HMStringItem {
			"MIT Kerberos Library\n\n"
			"This shared library provides Kerberos v5, "
			"Generic Security Services (GSS), and DES services."
		}
	}
};

resource 'CCI�' (128, purgeable) {
	"A shared library which provides Kerberos v5, "
	"Generic Security Services (GSS), and DES services."
};

include "Icons.rsrc";