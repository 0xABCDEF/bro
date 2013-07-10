// See the file "COPYING" in the main distribution directory for copyright.

#ifndef FILE_ANALYZER_PLUGIN_COMPONENT_H
#define FILE_ANALYZER_PLUGIN_COMPONENT_H

#include "analyzer/Tag.h"
#include "plugin/Component.h"

#include "Val.h"

#include "../config.h"
#include "../util.h"

namespace file_analysis {

class File;
class Analyzer;

/**
 * Component description for plugins providing file analyzers.
 *
 * A plugin can provide a specific file analyzer by registering this
 * analyzer component, describing the analyzer.
 */
class Component : public plugin::Component {
public:
	typedef Analyzer* (*factory_callback)(RecordVal* args, File* file);

	/**
	 * Constructor.
	 *
	 * @param name The name of the provided analyzer. This name is used
	 * across the system to identify the analyzer, e.g., when calling
	 * file_analysis::Manager::InstantiateAnalyzer with a name.
	 *
	 * @param factory A factory function to instantiate instances of the
	 * analyzer's class, which must be derived directly or indirectly
	 * from file_analysis::Analyzer. This is typically a static \c
	 * Instatiate() method inside the class that just allocates and
	 * returns a new instance.
	 *
	 * @param subtype A subtype associated with this component that
	 * further distinguishes it. The subtype will be integrated into
	 * the analyzer::Tag that the manager associates with this analyzer,
	 * and analyzer instances can accordingly access it via analyzer::Tag().
	 * If not used, leave at zero.
	 */
	Component(const char* name, factory_callback factory,
	          analyzer::Tag::subtype_t subtype = 0);

	/**
	 * Copy constructor.
	 */
	Component(const Component& other);

	/**
	 * Destructor.
	 */
	~Component();

	/**
	 * Returns the name of the analyzer. This name is unique across all
	 * analyzers and used to identify it. The returned name is derived
	 * from what's passed to the constructor but upper-cased and
	 * canonified to allow being part of a script-level ID.
	 */
	const char* Name() const	{ return name; }

	/**
	 * Returns a canonocalized version of the analyzer's name.  The
	 * returned name is derived from what's passed to the constructor but
	 * upper-cased and transformed to allow being part of a script-level
	 * ID.
	 */
	const char* CanonicalName() const	{ return canon_name; }

	/**
	 * Returns the analyzer's factory function.
	 */
	factory_callback Factory() const	{ return factory; }

	/**
	 * Returns the analyzer's tag. Note that this is automatically
	 * generated for each new Components, and hence unique across all of
	 * them.
	 */
	analyzer::Tag Tag() const;

	/**
	 * Generates a human-readable description of the component's main
	 * parameters. This goes into the output of \c "bro -NN".
	 */
	virtual void Describe(ODesc* d);

	Component& operator=(const Component& other);

private:
	const char* name;	// The analyzer's name.
	const char* canon_name;	// The analyzer's canonical name.
	factory_callback factory;	// The analyzer's factory callback.
	analyzer::Tag tag;	// The automatically assigned analyzer tag.

	// Global counter used to generate unique tags.
	static analyzer::Tag::type_t type_counter;
};

}

#endif
