/*	--*- c -*--
 * Copyright (C) 2011 Enrico Scholz <enrico.scholz@informatik.tu-chemnitz.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef H_ENSC_SNMPD_DISKIO_CONFIG_H
#define H_ENSC_SNMPD_DISKIO_CONFIG_H

#define	_GNU_SOURCE

#ifndef PREFIX
#  define PREFIX		"/usr/local"
#endif

#ifndef SYSCONFDIR
#  define SYSCONFDIR		PREFIX "/etc"
#endif

#ifndef LIBEXECDIR
#  define LIBEXECDIR		PREFIX "/libexec"
#endif

#ifndef LOCALSTATEDIR
#  define LOCALSTATEDIR		"/var/run"
#endif

#ifndef CONF_FILE
#  define CONF_FILE		SYSCONFDIR "/snmp/diskio.conf"
#endif

#ifndef CACHE_FILE
#  define CACHE_FILE		LOCALSTATEDIR "/snmpd-diskio/volumes"
#endif

#ifndef CACHE_REGEN_PROG
#  define CACHE_REGEN_PROG	LIBEXECDIR "/snmpd-diskio-cache"
#endif

#endif	/* H_ENSC_SNMPD_DISKIO_CONFIG_H */
