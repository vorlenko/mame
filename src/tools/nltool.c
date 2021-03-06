// license:GPL-2.0+
// copyright-holders:Couriersud
/***************************************************************************

    nltool.c

    Simple tool to debug netlists outside MAME.

****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sstream>
#include <assert.h>
#include "corefile.h"
#include "corestr.h"
#include "sha1.h"
#include "netlist/nl_base.h"
#include "netlist/nl_setup.h"
#include "netlist/nl_parser.h"
#include "netlist/nl_factory.h"
#include "netlist/nl_util.h"
#include "netlist/devices/net_lib.h"
#include "options.h"

/***************************************************************************
 * MAME COMPATIBILITY ...
 *
 * These are needed if we link without libutil
 ***************************************************************************/

#if 0
void ATTR_PRINTF(1,2) osd_printf_warning(const char *format, ...)
{
	va_list argptr;

	/* do the output */
	va_start(argptr, format);
	vprintf(format, argptr);
	va_end(argptr);
}

void *malloc_file_line(size_t size, const char *file, int line)
{
	// allocate the memory and fail if we can't
	void *ret = osd_malloc(size);
	memset(ret, 0, size);
	return ret;
}

void *malloc_array_file_line(size_t size, const char *file, int line)
{
	// allocate the memory and fail if we can't
	void *ret = osd_malloc_array(size);
	memset(ret, 0, size);
	return ret;
}

void free_file_line( void *memory, const char *file, int line )
{
	osd_free( memory );
}

void CLIB_DECL logerror(const char *format, ...)
{
	va_list arg;
	va_start(arg, format);
	vprintf(format, arg);
	va_end(arg);
}

void report_bad_cast(const std::type_info &src_type, const std::type_info &dst_type)
{
	printf("Error: bad downcast<> or device<>.  Tried to convert a %s to a %s, which are incompatible.\n",
			src_type.name(), dst_type.name());
	throw;
}
#endif

struct options_entry oplist[] =
{
	{ "time_to_run;t",   "1.0", OPTION_FLOAT,   "time to run the emulation (seconds)" },
	{ "logs;l",          "",    OPTION_STRING,  "colon separated list of terminals to log" },
	{ "file;f",          "-",   OPTION_STRING,  "file to process (default is stdin)" },
	{ "cmd;c",			 "run", OPTION_STRING,  "run|convert|listdevices" },
	{ "listdevices;ld",  "",    OPTION_BOOLEAN, "list all devices available for use" },
	{ "verbose;v",       "0",   OPTION_BOOLEAN, "list all devices available for use" },
	{ "help;h",          "0",   OPTION_BOOLEAN, "display help" },
	{ NULL, NULL, 0, NULL }
};

NETLIST_START(dummy)
	/* Standard stuff */

	CLOCK(clk, 1000) // 1000 Hz
	SOLVER(Solver, 48000)

NETLIST_END()

/***************************************************************************
    CORE IMPLEMENTATION
***************************************************************************/

pstring filetobuf(pstring fname)
{
	pstring pbuf = "";

	if (fname == "-")
	{
		char lbuf[1024];
		while (!feof(stdin))
		{
			fgets(lbuf, 1024, stdin);
			pbuf += lbuf;
		}
		printf("%d\n",*(pbuf.right(1).cstr()+1));
		return pbuf;
	}
	else
	{
		FILE *f;
		f = fopen(fname, "rb");
		fseek(f, 0, SEEK_END);
		long fsize = ftell(f);
		fseek(f, 0, SEEK_SET);

		char *buf = (char *) malloc(fsize + 1);
		fread(buf, fsize, 1, f);
		buf[fsize] = 0;
		fclose(f);
		pbuf = buf;
		free(buf);
		return pbuf;
	}
}

class netlist_tool_t : public netlist_base_t
{
public:

	netlist_tool_t()
	: netlist_base_t(), m_logs(""), m_verbose(false), m_setup(NULL)
	{
	}

	virtual ~netlist_tool_t() { };

	void init()
	{
		m_setup = nl_alloc(netlist_setup_t, *this);
		this->init_object(*this, "netlist");
		m_setup->init();
	}

	void read_netlist(const char *buffer)
	{
		// read the netlist ...

		netlist_sources_t sources;

		sources.add(netlist_source_t(buffer));
		sources.parse(*m_setup,"");
		//m_setup->parse(buffer);
		log_setup();

		// start devices
		m_setup->start_devices();
		m_setup->resolve_inputs();
		// reset
		this->reset();
	}

	void log_setup()
	{
		NL_VERBOSE_OUT(("Creating dynamic logs ...\n"));
		nl_util::pstring_list ll = nl_util::split(m_logs, ":");
		for (int i=0; i < ll.count(); i++)
		{
			pstring name = "log_" + ll[i];
			/*netlist_device_t *nc = */ m_setup->register_dev("nld_log", name);
			m_setup->register_link(name + ".I", ll[i]);
		}
	}

	pstring m_logs;

	bool m_verbose;

protected:

	void verror(const loglevel_e level, const char *format, va_list ap) const
	{
		switch (level)
		{
			case NL_LOG:
				if (m_verbose)
				{
					vprintf(format, ap);
					printf("\n");
				}
				break;
			case NL_WARNING:
				vprintf(format, ap);
				printf("\n");
				break;
			case NL_ERROR:
				vprintf(format, ap);
				printf("\n");
				throw;
		}
	}

private:
	netlist_setup_t *m_setup;
};


void usage(core_options &opts)
{
	std::string buffer;
	fprintf(stderr,
		"Usage:\n"
		"  nltool -help\n"
		"  nltool [options]\n"
		"\n"
		"Where:\n"
	);
	fprintf(stderr, "%s\n", opts.output_help(buffer));
}

static void run(core_options &opts)
{
	netlist_tool_t nt;
	osd_ticks_t t = osd_ticks();

	nt.init();
	nt.m_logs = opts.value("l");
	nt.m_verbose = opts.bool_value("v");
	nt.read_netlist(filetobuf(opts.value("f")));
	double ttr = opts.float_value("t");

	printf("startup time ==> %5.3f\n", (double) (osd_ticks() - t) / (double) osd_ticks_per_second() );
	printf("runnning ...\n");
	t = osd_ticks();

	nt.process_queue(netlist_time::from_double(ttr));
	nt.stop();

	double emutime = (double) (osd_ticks() - t) / (double) osd_ticks_per_second();
	printf("%f seconds emulation took %f real time ==> %5.2f%%\n", ttr, emutime, ttr/emutime*100.0);
}

/*-------------------------------------------------
    listdevices - list all known devices
-------------------------------------------------*/

static void listdevices()
{
	netlist_tool_t nt;
	nt.init();
	const netlist_factory_t::list_t &list = nt.setup().factory().list();

	netlist_sources_t sources;

	sources.add(netlist_source_t("dummy", &netlist_dummy));
	sources.parse(nt.setup(),"dummy");

	nt.setup().start_devices();
	nt.setup().resolve_inputs();

	for (int i=0; i < list.count(); i++)
	{
		pstring out = pstring::sprintf("%-20s %s(<id>", list[i]->classname().cstr(),
				list[i]->name().cstr() );
		pstring terms("");

		net_device_t_base_factory *f = list[i];
		netlist_device_t *d = f->Create();
		d->init(nt, pstring::sprintf("dummy%d", i));
		d->start_dev();

		// get the list of terminals ...
		for (int j=0; j < d->m_terminals.count(); j++)
		{
			pstring inp = d->m_terminals[j];
			if (inp.startsWith(d->name() + "."))
				inp = inp.substr(d->name().len() + 1);
			terms += "," + inp;
		}

		if (list[i]->param_desc().startsWith("+"))
		{
			out += "," + list[i]->param_desc().substr(1);
			terms = "";
		}
		else if (list[i]->param_desc() == "-")
		{
			/* no params at all */
		}
		else
		{
			out += "," + list[i]->param_desc();
		}
		out += ")";
		printf("%s\n", out.cstr());
		if (terms != "")
			printf("Terminals: %s\n", terms.substr(1).cstr());
	}
}

/*-------------------------------------------------
    convert - convert a spice netlist
-------------------------------------------------*/

class convert_t
{
public:
	void convert(pstring contents)
	{
		nl_util::pstring_list spnl = nl_util::split(contents, "\n");

		// Add gnd net

		nets.add(nl_alloc(sp_net_t, "0"), false);
		nets[0]->terminals().add("GND");

		pstring line = "";

		for (int i=0; i < spnl.count(); i++)
		{
			// Basic preprocessing
			pstring inl = spnl[i].trim().ucase();
			if (inl.startsWith("+"))
				line = line + inl.substr(1);
			else
			{
				process_line(line);
				line = inl;
			}
		}
		process_line(line);
		dump_nl();
	}

protected:
	struct sp_net_t
	{
	public:
		sp_net_t(const pstring &aname)
		: m_name(aname), m_no_export(false) {}

		const pstring &name() { return m_name;}
		nl_util::pstring_list &terminals() { return m_terminals; }
		void set_no_export() { m_no_export = true; }
		bool is_no_export() { return m_no_export; }

	private:
		pstring m_name;
		bool m_no_export;
		nl_util::pstring_list m_terminals;
	};

	struct sp_dev_t
	{
	public:
		sp_dev_t(const pstring atype, const pstring aname, const pstring amodel)
		: m_type(atype), m_name(aname), m_model(amodel), m_val(0), m_has_val(false)
		{}

		sp_dev_t(const pstring atype, const pstring aname, double aval)
		: m_type(atype), m_name(aname), m_model(""), m_val(aval), m_has_val(true)
		{}

		sp_dev_t(const pstring atype, const pstring aname)
		: m_type(atype), m_name(aname), m_model(""), m_val(0.0), m_has_val(false)
		{}

		const pstring &name() { return m_name;}
		const pstring &type() { return m_type;}
		const pstring &model() { return m_model;}
		const double &value() { return m_val;}

		bool has_model() { return m_model != ""; }
		bool has_value() { return m_has_val; }

	private:
		pstring m_type;
		pstring m_name;
		pstring m_model;
		double m_val;
		bool m_has_val;
	};

	struct sp_unit {
		pstring sp_unit;
		pstring nl_func;
		double mult;
	};


	void add_term(pstring netname, pstring termname)
	{
		sp_net_t * net = nets.find(netname);
		if (net == NULL)
		{
			net = nl_alloc(sp_net_t, netname);
			nets.add(net, false);
		}
		net->terminals().add(termname);
	}

	const pstring get_nl_val(const double val)
	{
		{
			int i = 0;
			while (m_sp_units[i].sp_unit != "-" )
			{
				if (m_sp_units[i].mult <= nl_math::abs(val))
					break;
				i++;
			}
			return pstring::sprintf(m_sp_units[i].nl_func.cstr(), val / m_sp_units[i].mult);
		}
	}
	double get_sp_unit(const pstring &unit)
	{
		int i = 0;
		while (m_sp_units[i].sp_unit != "-")
		{
			if (m_sp_units[i].sp_unit == unit)
				return m_sp_units[i].mult;
			i++;
		}
		fprintf(stderr, "Unit %s unknown\n", unit.cstr());
		return 0.0;
	}

	double get_sp_val(const pstring &sin)
	{
		int p = sin.len() - 1;
		while (p>=0 && (sin.substr(p,1) < "0" || sin.substr(p,1) > "9"))
			p--;
		pstring val = sin.substr(0,p + 1);
		pstring unit = sin.substr(p + 1);

		double ret = get_sp_unit(unit) * val.as_double();
		//printf("<%s> %s %d ==> %f\n", sin.cstr(), unit.cstr(), p, ret);
		return ret;
	}

	void dump_nl()
	{
		for (int i=0; i<alias.count(); i++)
		{
			sp_net_t *net = nets.find(alias[i]);
			// use the first terminal ...
			printf("ALIAS(%s, %s)\n", alias[i].cstr(), net->terminals()[0].cstr());
			// if the aliased net only has this one terminal connected ==> don't dump
			if (net->terminals().count() == 1)
				net->set_no_export();
		}
		for (int i=0; i<devs.count(); i++)
		{
			if (devs[i]->has_value())
				printf("%s(%s, %s)\n", devs[i]->type().cstr(),
						devs[i]->name().cstr(), get_nl_val(devs[i]->value()).cstr());
			else if (devs[i]->has_model())
				printf("%s(%s, \"%s\")\n", devs[i]->type().cstr(),
						devs[i]->name().cstr(), devs[i]->model().cstr());
			else
				printf("%s(%s)\n", devs[i]->type().cstr(),
						devs[i]->name().cstr());
		}
		// print nets
		for (int i=0; i<nets.count(); i++)
		{
			sp_net_t * net = nets[i];
			if (!net->is_no_export())
			{
				//printf("Net %s\n", net->name().cstr());
				printf("NET_C(%s", net->terminals()[0].cstr() );
				for (int j=1; j<net->terminals().count(); j++)
				{
					printf(", %s", net->terminals()[j].cstr() );
				}
				printf(")\n");
			}
		}
		devs.clear_and_free();
		nets.clear_and_free();
		alias.clear();
	}

	void process_line(const pstring &line)
	{
		if (line != "")
		{
			nl_util::pstring_list tt = nl_util::split(line, " ", true);
			switch (tt[0].cstr()[0])
			{
				case ';':
					printf("// %s\n", line.substr(1).cstr());
					break;
				case '*':
					printf("// %s\n", line.substr(1).cstr());
					break;
				case '.':
					if (tt[0].equals(".SUBCKT"))
					{
						printf("NETLIST_START(%s)\n", tt[1].cstr());
						for (int i=2; i<tt.count(); i++)
							alias.add(tt[i]);
					}
					else if (tt[0].equals(".ENDS"))
					{
						dump_nl();
						printf("NETLIST_END()\n");
					}
					else
						printf("// %s\n", line.cstr());
					break;
				case 'Q':
				{
					bool cerr = false;
					/* check for fourth terminal ... should be numeric net
					 * including "0" or start with "N" (ltspice)
					 */
					int nval =tt[4].as_long(&cerr);
					if ((!cerr || tt[4].startsWith("N")) && tt.count() > 5)
						devs.add(nl_alloc(sp_dev_t, "QBJT", tt[0], tt[5]), false);
					else
						devs.add(nl_alloc(sp_dev_t, "QBJT", tt[0], tt[4]), false);
					add_term(tt[1], tt[0] + ".C");
					add_term(tt[2], tt[0] + ".B");
					add_term(tt[3], tt[0] + ".E");
				}
					break;
				case 'R':
					devs.add(nl_alloc(sp_dev_t, "RES", tt[0], get_sp_val(tt[3])), false);
					add_term(tt[1], tt[0] + ".1");
					add_term(tt[2], tt[0] + ".2");
					break;
				case 'C':
					devs.add(nl_alloc(sp_dev_t, "CAP", tt[0], get_sp_val(tt[3])), false);
					add_term(tt[1], tt[0] + ".1");
					add_term(tt[2], tt[0] + ".2");
					break;
				case 'V':
					// just simple Voltage sources ....
					if (tt[2].equals("0"))
					{
						devs.add(nl_alloc(sp_dev_t, "ANALOG_INPUT", tt[0], get_sp_val(tt[3])), false);
						add_term(tt[1], tt[0] + ".Q");
						//add_term(tt[2], tt[0] + ".2");
					}
					else
						fprintf(stderr, "Voltage Source %s not connected to GND\n", tt[0].cstr());
					break;
				case 'D':
					// FIXME: Rewrite resistor value
					devs.add(nl_alloc(sp_dev_t, "DIODE", tt[0], tt[3]), false);
					add_term(tt[1], tt[0] + ".A");
					add_term(tt[2], tt[0] + ".K");
					break;
				case 'X':
					// FIXME: specific code for KICAD exports
					//        last element is component type
					devs.add(nl_alloc(sp_dev_t, "TTL_" + tt[tt.count()-1] + "_DIP", tt[0]), false);
					for (int i=1; i < tt.count() - 1; i++)
					{
						pstring term = pstring::sprintf("%s.%d", tt[0].cstr(), i);
						add_term(tt[i], term);
					}
					break;
				default:
					printf("// IGNORED %s: %s\n", tt[0].cstr(), line.cstr());
			}
		}
	}


private:
	pnamedlist_t<sp_net_t *> nets;
	pnamedlist_t<sp_dev_t *> devs;
	plist_t<pstring> alias;

	static sp_unit m_sp_units[];
};

convert_t::sp_unit convert_t::m_sp_units[] = {
		{"T",   "",      1.0e12	},
		{"G",   "", 	 1.0e9	},
		{"MEG", "RES_M(%g)", 1.0e6	},
		{"K",   "RES_K(%g)", 1.0e3	},
		{"",    "%g",        1.0e0 	},
		{"M",   "CAP_M(%g)", 1.0e-3	},
		{"U",   "CAP_U(%g)", 1.0e-6	},
		{"µ",   "CAP_U(%g)", 1.0e-6	},
		{"N",   "CAP_N(%g)", 1.0e-9	},
		{"P",   "CAP_P(%g)", 1.0e-12},
		{"F", 	"%ge-15",    1.0e-15},

		{"MIL", "%e",  25.4e-6},

		{"-", 	"%g",  1.0	}
};


/*-------------------------------------------------
    main - primary entry point
-------------------------------------------------*/

int main(int argc, char *argv[])
{
	//int result;
	core_options opts(oplist);
	std::string aerror("");

	fprintf(stderr, "%s", "WARNING: This is Work In Progress! - It may fail anytime\n");
	if (!opts.parse_command_line(argc, argv, OPTION_PRIORITY_DEFAULT, aerror))
	{
		fprintf(stderr, "%s\n", aerror.c_str());
		usage(opts);
		return 1;
	}

	if (opts.bool_value("h"))
	{
		usage(opts);
		return 1;
	}

	pstring cmd = opts.value("c");
	if (cmd == "listdevices")
		listdevices();
	else if (cmd == "run")
		run(opts);
	else if (cmd == "convert")
	{
		pstring contents = filetobuf(opts.value("f"));
		convert_t converter;
		converter.convert(contents);
	}
	else
	{
		fprintf(stderr, "Unknown command %s\n", cmd.cstr());
		usage(opts);
		return 1;
	}

	return 0;
}
