// See the file "COPYING" in the main distribution directory for copyright.

#include "config.h"

#ifdef USE_ELASTICSEARCH

#include <string>
#include <errno.h>

#include "util.h"
#include "BroString.h"

#include "NetVar.h"
#include "threading/SerialTypes.h"

#include <curl/curl.h>
#include <curl/easy.h>

#include "ElasticSearch.h"

using namespace logging;
using namespace writer;
using threading::Value;
using threading::Field;

ElasticSearch::ElasticSearch(WriterFrontend* frontend) : WriterBackend(frontend)
	{
	cluster_name_len = BifConst::LogElasticSearch::cluster_name->Len();
	cluster_name = new char[cluster_name_len + 1];
	memcpy(cluster_name, BifConst::LogElasticSearch::cluster_name->Bytes(), cluster_name_len);
	cluster_name[cluster_name_len] = 0;
	
	index_prefix = string((const char*) BifConst::LogElasticSearch::index_prefix->Bytes(), BifConst::LogElasticSearch::index_prefix->Len());
	
	es_server = string(fmt("http://%s:%d/", BifConst::LogElasticSearch::server_host->Bytes(),
	                                        (int) BifConst::LogElasticSearch::server_port));
	
	buffer.Clear();
	counter = 0;
	current_index = string();
	prev_index = string();
	last_send = current_time();
	
	curl_handle = HTTPSetup();
}

ElasticSearch::~ElasticSearch()
	{
	delete [] cluster_name;
	}

bool ElasticSearch::DoInit(const WriterInfo& info, int num_fields, const threading::Field* const* fields)
	{
	return true;
	}

bool ElasticSearch::DoFlush()
	{
	// Do something here?
	return true;
	}

bool ElasticSearch::DoFinish()
	{
	BatchIndex();
	curl_easy_cleanup(curl_handle);
	return WriterBackend::DoFinish();
	}
	
bool ElasticSearch::BatchIndex()
	{
	curl_easy_reset(curl_handle);
	string url = es_server + "_bulk";
	curl_easy_setopt(curl_handle, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl_handle, CURLOPT_POST, 1);
	curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)buffer.Len());
	curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, buffer.Bytes());
	HTTPSend(curl_handle);
	
	buffer.Clear();
	counter = 0;
	last_send = current_time();
	
	return true;
	}

bool ElasticSearch::AddValueToBuffer(ODesc* b, Value* val)
	{
	switch ( val->type ) 
		{
		// ES treats 0 as false and any other value as true so bool types go here.
		case TYPE_BOOL:
		case TYPE_INT:
			b->Add(val->val.int_val);
			break;
		
		case TYPE_COUNT:
		case TYPE_COUNTER:
			{
			// ElasticSearch doesn't seem to support unsigned 64bit ints.
			if ( val->val.uint_val >= INT64_MAX )
				{
				Error(Fmt("count value too large: %" PRIu64, val->val.uint_val));
				b->AddRaw("null", 4);
				}
			else
				b->Add(val->val.uint_val);
			break;
			}
		
		case TYPE_PORT:
			b->Add(val->val.port_val.port);
			break;
		
		case TYPE_SUBNET:
			b->AddRaw("\"", 1);
			b->Add(Render(val->val.subnet_val));
			b->AddRaw("\"", 1);
			break;
		
		case TYPE_ADDR:
			b->AddRaw("\"", 1);
			b->Add(Render(val->val.addr_val));
			b->AddRaw("\"", 1);
			break;
		
		case TYPE_DOUBLE:
		case TYPE_INTERVAL:
			b->Add(val->val.double_val);
			break;
		
		case TYPE_TIME:
			{
			// ElasticSearch uses milliseconds for timestamps and json only
			// supports signed ints (uints can be too large).
			uint64_t ts = (uint64_t) (val->val.double_val * 1000);
			if ( ts >= INT64_MAX )
				{
				Error(Fmt("time value too large: %" PRIu64, ts));
				b->AddRaw("null", 4);
				}
			else
				b->Add(ts);
			break;
			}
		
		case TYPE_ENUM:
		case TYPE_STRING:
		case TYPE_FILE:
		case TYPE_FUNC:
			{
			b->AddRaw("\"", 1);
			for ( uint i = 0; i < val->val.string_val->size(); ++i )
				{
				char c = val->val.string_val->data()[i];
				// 2byte Unicode escape special characters.
				if ( c < 32 || c > 126 || c == '\n' || c == '"' || c == '\'' || c == '\\' || c == '&' )
					{
					static const char hex_chars[] = "0123456789abcdef";
					b->AddRaw("\\u00", 4);
					b->AddRaw(&hex_chars[(c & 0xf0) >> 4], 1);
					b->AddRaw(&hex_chars[c & 0x0f], 1);
					}
				else
					b->AddRaw(&c, 1);
				}
			b->AddRaw("\"", 1);
			break;
			}
		
		case TYPE_TABLE:
			{
			b->AddRaw("[", 1);
			for ( int j = 0; j < val->val.set_val.size; j++ )
				{
				if ( j > 0 )
					b->AddRaw(",", 1);
				AddValueToBuffer(b, val->val.set_val.vals[j]);
				}
			b->AddRaw("]", 1);
			break;
			}
			
		case TYPE_VECTOR:
			{
			b->AddRaw("[", 1);
			for ( int j = 0; j < val->val.vector_val.size; j++ )
				{
				if ( j > 0 )
					b->AddRaw(",", 1);
				AddValueToBuffer(b, val->val.vector_val.vals[j]);
				}
			b->AddRaw("]", 1);
			break;
			}
		
		default:
			return false;
		}
	return true;
	}

bool ElasticSearch::AddFieldToBuffer(ODesc *b, Value* val, const Field* field)
	{
	if ( ! val->present )
		return false;
	
	b->AddRaw("\"", 1);
	b->Add(field->name);
	b->AddRaw("\":", 2);
	AddValueToBuffer(b, val);
	return true;
	}

bool ElasticSearch::DoWrite(int num_fields, const Field* const * fields,
			     Value** vals)
	{
	if ( current_index.empty() )
		UpdateIndex(network_time, Info().rotation_interval, Info().rotation_base);
	
	// Our action line looks like:
	buffer.AddRaw("{\"index\":{\"_index\":\"", 20);
	buffer.Add(current_index);
	buffer.AddRaw("\",\"_type\":\"", 11);
	buffer.Add(Info().path);
	buffer.AddRaw("\"}\n", 3);
	
	buffer.AddRaw("{", 1);
	for ( int i = 0; i < num_fields; i++ )
		{
		if ( i > 0 && buffer.Bytes()[buffer.Len()] != ',' && vals[i]->present )
			buffer.AddRaw(",", 1);
		AddFieldToBuffer(&buffer, vals[i], fields[i]);
		}
	buffer.AddRaw("}\n", 2);
	
	counter++;
	if ( counter >= BifConst::LogElasticSearch::max_batch_size ||
	     uint(buffer.Len()) >= BifConst::LogElasticSearch::max_byte_size )
		BatchIndex();
	
	return true;
	}
	
bool ElasticSearch::UpdateIndex(double now, double rinterval, double rbase)
	{
	if ( rinterval == 0 )
		{
		// if logs aren't being rotated, don't use a rotation oriented index name.
		current_index = index_prefix;
		}
	else
		{
		double nr = calc_next_rotate(now, rinterval, rbase);
		double interval_beginning = now - (rinterval - nr);
		
		struct tm tm;
		char buf[128];
		time_t teatime = (time_t)interval_beginning;
		gmtime_r(&teatime, &tm);
		strftime(buf, sizeof(buf), "%Y%m%d%H%M", &tm);
		
		prev_index = current_index;
		current_index = index_prefix + "-" + buf;
		}
	
	//printf("%s - prev:%s current:%s\n", Info().path.c_str(), prev_index.c_str(), current_index.c_str());
	return true;
	}
	

bool ElasticSearch::DoRotate(string rotated_path, const RotateInfo& info, bool terminating)
	{
	// Update the currently used index to the new rotation interval.
	UpdateIndex(info.close, info.interval, info.base_time);
	
	// Only do this stuff if there was a previous index.
	if ( ! prev_index.empty() )
		{
		// FIXME: I think this section is taking too long and causing the thread to die.
		
		// Compress the previous index
		//curl_easy_reset(curl_handle);
		//curl_easy_setopt(curl_handle, CURLOPT_URL, fmt("%s%s/_settings", es_server.c_str(), prev_index.c_str()));
		//curl_easy_setopt(curl_handle, CURLOPT_CUSTOMREQUEST, "PUT");
		//curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, "{\"index\":{\"store.compress.stored\":\"true\"}}");
		//curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t) 42);
		//HTTPSend(curl_handle);
		
		// Optimize the previous index.
		// TODO: make this into variables.
		//curl_easy_reset(curl_handle);
		//curl_easy_setopt(curl_handle, CURLOPT_URL, fmt("%s%s/_optimize?max_num_segments=1&wait_for_merge=false", es_server.c_str(), prev_index.c_str()));
		//HTTPSend(curl_handle);
		}
	
	if ( ! FinishedRotation(current_index, prev_index, info, terminating) )
		{
		Error(Fmt("error rotating %s to %s", prev_index.c_str(), current_index.c_str()));
		}
	
	return true;
	}

bool ElasticSearch::DoSetBuf(bool enabled)
	{
	// Nothing to do.
	return true;
	}

bool ElasticSearch::DoHeartbeat(double network_time, double current_time)
	{
	if ( last_send > 0 && buffer.Len() > 0 &&
	     current_time-last_send > BifConst::LogElasticSearch::max_batch_interval )
		{
		BatchIndex();
		}
	
	return WriterBackend::DoHeartbeat(network_time, current_time);
	}


CURL* ElasticSearch::HTTPSetup()
	{
	CURL* handle = curl_easy_init();
	if ( ! handle )
		{
		Error(fmt("cURL did not initialize correctly."));
		return 0;
		}
	
	return handle;
	}

bool ElasticSearch::HTTPReceive(void* ptr, int size, int nmemb, void* userdata)
	{
	//TODO: Do some verification on the result?
	return true;
	}

bool ElasticSearch::HTTPSend(CURL *handle)
	{
	struct curl_slist *headers = curl_slist_append(NULL, "Content-Type: text/json; charset=utf-8");
	curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, &logging::writer::ElasticSearch::HTTPReceive); // This gets called with the result.
	// HTTP 1.1 likes to use chunked encoded transfers, which aren't good for speed. 
	// The best (only?) way to disable that is to just use HTTP 1.0
	curl_easy_setopt(handle, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_0);
	
	CURLcode return_code = curl_easy_perform(handle);
	
	switch ( return_code ) 
		{
		case CURLE_COULDNT_CONNECT:
		case CURLE_COULDNT_RESOLVE_HOST:
		case CURLE_WRITE_ERROR:
			return false;
		
		case CURLE_OK:
			{
			uint http_code = 0;
			curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &http_code);
			if ( http_code != 200 )
				Error(fmt("Received a non-successful status code back from ElasticSearch server."));
			
			return true;
			}
		
		default:
			return true;
		}
	}

#endif
