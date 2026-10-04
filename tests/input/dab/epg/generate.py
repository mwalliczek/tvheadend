#!/usr/bin/env python3
"""
Generates the DAB EPG test vectors with the reference implementations
hybridspi (ETSI TS 102 371 binary encoding), dabmot (MOT) and dabmsc
(MSC data groups / packet mode), see https://github.com/magicbadger.

Those libraries are Python 2; to run this script convert them with
"python3 -m lib2to3 -w -n spi mot msc", replace bitarray tostring/fromstring
by tobytes/frombytes and put them into PYTHONPATH.
"""
import datetime, gzip, os, sys
from dateutil.tz import tzutc

from spi import *
from spi.binary import marshall
from mot import MotObject, ContentType, ContentName, Compression
from msc.datagroups import encode_headermode, encode_directorymode
from msc.packets import encode_packets

out = os.path.dirname(os.path.abspath(__file__))

# one schedule for service D312 (Bayern 3) with two programmes
# note: hybridspi computes the MJD one day too early for times before 12:00 UTC
schedule = Schedule(created=datetime.datetime(2026, 10, 4, 6, 0, tzinfo=tzutc()))
p1 = Programme('crid://br.de/12345', 0x123456)
p1.names.append(ShortName('Morgen'))
p1.names.append(MediumName('Frühaufdreher'))
p1.names.append(LongName('Bayern 3 - Die Frühaufdreher'))
p1.descriptions.append(ShortDescription('Gut gelaunt in den Tag'))
p1.descriptions.append(LongDescription('Mit Nachrichten, Wetter und Verkehr für ganz Bayern.'))
p1.genres.append(Genre('urn:tva:metadata:cs:ContentCS:2002:3.6.8'))
loc = Location()
loc.times.append(Time(datetime.datetime(2026, 10, 4, 13, 0, tzinfo=tzutc()),
                      datetime.timedelta(hours=4)))
loc.bearers.append(DabBearer(0xE0, 0x10C1, 0xD312))
p1.locations.append(loc)
schedule.programmes.append(p1)

p2 = Programme('crid://br.de/12346', 0x123457)
p2.names.append(MediumName('Mittag'))
loc = Location()
loc.times.append(Time(datetime.datetime(2026, 10, 4, 17, 0, 30, tzinfo=tzutc()),
                      datetime.timedelta(minutes=90)))
loc.bearers.append(DabBearer(0xE0, 0x10C1, 0xD312))
p2.locations.append(loc)
schedule.programmes.append(p2)

info = ProgrammeInfo(schedules=[schedule])
doc = marshall(info)
open(os.path.join(out, 'pi.bin'), 'wb').write(doc)

def write_packets(name, datagroups, address=1, size=96):
    data = b''.join(p.tobytes() for p in encode_packets(datagroups, address=address, size=size))
    open(os.path.join(out, name), 'wb').write(data)

# header mode, uncompressed
obj = MotObject('20261004_D312_PI.EHB', doc, ContentType(7, 1), transport_id=0x1234)
write_packets('packets_header.bin', encode_headermode([obj]), size=96)

# directory mode with a gzip compressed body and small segments
gz = gzip.compress(doc)
obj = MotObject('20261004_D312_PI.EHB', gz, ContentType(7, 1), transport_id=0x2345)
obj.add_parameter(Compression.GZIP)
from msc.datagroups import ConstantSegmentSize
write_packets('packets_directory.bin',
              encode_directorymode([obj], segmenting_strategy=ConstantSegmentSize(100)),
              address=2, size=48)
