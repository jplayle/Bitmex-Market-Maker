#pragma once

#include <string>
#include <sstream>
#include <openssl/evp.h>
#include <openssl/hmac.h>

// data str for bal: "GET/api/v1/user/wallet" + valid_till;
// data str for upd: "PUT/api/v1/order"  + valid_till + update_order_msg;
// data str for new: "POST/api/v1/order" + valid_till + limit_order_msg;


struct Auth
{
    const char* apiKeyCStr = "";
    const char* apiSecCStr = "";

	const int apiKeyLen = 24;
	const int apiSecLen = 48;
	
	const int msg_t_expiry = 10;
	
	
	std::string HMAC_SHA256_hex(const std::string& url, std::string& valid_till, std::string& o_msg)
    {
		std::string msg_data = url + valid_till + o_msg;
		
        std::stringstream ss;
        unsigned int len;
        unsigned char out[EVP_MAX_MD_SIZE];
        HMAC_CTX *ctx = HMAC_CTX_new();
        HMAC_Init_ex(ctx, apiSecCStr, apiSecLen, EVP_sha256(), NULL);
        HMAC_Update(ctx, (unsigned char*)msg_data.c_str(), msg_data.length());
        HMAC_Final(ctx, out, &len);
        HMAC_CTX_free(ctx);
        
        for (int i = 0; i < len; ++i)
        {
            ss << std::setw(2) << std::setfill('0') << std::hex << (unsigned int)out[i];
        }
        return ss.str();
    }
	
};