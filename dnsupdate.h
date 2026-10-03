/*
 *  OpenVPN-GUI -- A Windows GUI for OpenVPN.
 *
 *  Copyright (C) 2026 OpenVPN-GUI contributors
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program (see the file COPYING included with this
 *  distribution); if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef DNSUPDATE_H
#define DNSUPDATE_H

#include "options.h"

/*
 * Check whether a DNS domain is remembered for this connection and, if so,
 * silently update the server address from its DNS TXT record. Meant to be
 * called automatically before connecting. Fully silent and fail-open.
 */
void AutoUpdateServerFromDNS(connection_t *c);

/* Let the user set (or change) the remembered DNS domain for a connection. */
void SetDNSDomain(connection_t *c);

#endif /* DNSUPDATE_H */
