// See the file "COPYING" in the main distribution directory for copyright.
//
// Log writer for writing to an ElasticSearch database

#ifndef LOGGING_WRITER_ELASTICSEARCH_H
#define LOGGING_WRITER_ELASTICSEARCH_H

#include <curl/curl.h>
#include "../WriterBackend.h"

namespace logging { namespace writer {

class ElasticSearch : public WriterBackend {
public:
	ElasticSearch(WriterFrontend* frontend);
	~ElasticSearch();

	static WriterBackend* Instantiate(WriterFrontend* frontend)
		{ return new ElasticSearch(frontend); }
	static string LogExt();

protected:
	// Overidden from WriterBackend.

	virtual bool DoInit(const WriterInfo& info, int num_fields,
			    const threading::Field* const* fields);

	virtual bool DoWrite(int num_fields, const threading::Field* const* fields,
			     threading::Value** vals);
	virtual bool DoSetBuf(bool enabled);
	virtual bool DoRotate(string rotated_path, const RotateInfo& info,
			      bool terminating);
	virtual bool DoFlush();
	virtual bool DoFinish();
	virtual bool DoHeartbeat(double network_time, double current_time);

private:
	bool AddFieldToBuffer(ODesc *b, threading::Value* val, const threading::Field* field);
	bool AddValueToBuffer(ODesc *b, threading::Value* val);
	bool BatchIndex();
	bool SendMappings();
	bool UpdateIndex(double now, double rinterval, double rbase);
	
	CURL* HTTPSetup();
	bool HTTPReceive(void* ptr, int size, int nmemb, void* userdata);
	bool HTTPSend();
	
	// Buffers, etc.
	ODesc buffer;
	uint64 counter;
	double last_send;
	string current_index;
	string prev_index;
	
	CURL* curl_handle;
	
	// From scripts
	char* cluster_name;
	int cluster_name_len;
	
	string path;
	string index_name;
	
	uint64 batch_size;
};

}
}


#endif
