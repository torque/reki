#include "common.h"

int intlength(int input) {
	int length = 1;
	for(long i = 10; i < 10000000000; i*=10) {
		if(input < i) {
			return length;
		}
		length++;
	}
	return 10;
}

long long read_int(char *str, int str_size, const int base) {
	check(str_size <= 99, "Value is way too big, skipping.");

	char temp[100];
	memcpy(temp, str, str_size);
	temp[str_size] = '\0';

	char *endptr;
	long long num = strtoll(temp, &endptr, base);

	return num;

	error:
		return -1;
}

/* The info hash is stored in mongo, which is not binary string safe apparently,
so it needs to be parsed to a hexadecimal string rather than a binary one. */
int parse_info_hash(char *output, int output_length, char *input, int input_length) {
	int pos, i = 0;
	for(pos = 0; pos < input_length; pos++) {
		check(i <= output_length, "Hash is invalid (too long).")
		if (input[pos] == '%') {
			check(pos + 2 < input_length, "Hash is invalid (malformed).")
			input[pos + 1] = tolower(input[pos + 1]); // ensure lower case output
			input[pos + 2] = tolower(input[pos + 2]);
			memcpy(output + i, input + pos + 1, 2);
			pos += 2;
		} else {
			sprintf(output + i, "%02x", (int)input[pos]);
		}
		i += 2;
	}
	check(i == output_length, "Hash is invalid (too short).")
	return 0;

	error:
		return -1;
}

/* Peer id is only ever used with redis, which is binary string safe. */
int parse_peer_id(char *output, char *input, int input_length) {
	int pos, i = 0;
	for(pos = 0; pos < input_length; pos++) {
		if (input[pos] == '%') {
			check(pos + 2 < input_length, "String is improperly escaped.")

			int16_t temp = read_int(input + pos + 1, 2, 16);
			check(temp != -1, "Failed to parse %.*s as a number.", 2, input + pos * 2);

			memcpy(output + i, &temp, 1);
			pos += 2;
		} else {
			memcpy(output + i, input + pos, 1);
		}
		i++;
	}
	return 0;

	error:
		return -1;
}

/* Input is hex string containing only [0-9A-F]. It is also only used internally,
so we don't have to worry about malicious formatting here.
Every 2 bytes in the input string correspond to 1 in the output. */
int hex_to_string(char *output, char *input) {
	int pos,
	    input_length = strlen(input),
	    output_length = input_length/2;

	for (pos = 0; pos < output_length; pos++ ) {
		int temp = read_int(input + pos * 2, 2, 16);
		check(temp != -1, "Failed to parse %.*s as a number.", 2, input + pos * 2);
		memcpy(output + pos, &temp, 1); // this is probably horrible
	}
	return 0;

	error:
		return -1;
}

void simple_error(client_socket_data *data, char *message) {
	int message_length = strlen(message);
	int failure_reason_length = 20 + message_length + intlength(message_length);
	int http_response_length = 82 + failure_reason_length + intlength(failure_reason_length);

	char *failure_reason = malloc(sizeof(char)*(failure_reason_length+1));
	char *http_response = malloc(sizeof(char)*(http_response_length+1));
	check_mem(failure_reason);
	check_mem(http_response);

	sprintf(failure_reason,"d14:failure reason%u:%se", message_length, message);
	sprintf(http_response, "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\nContent-Length: %u\r\n\r\n%s", failure_reason_length, failure_reason);

	send(data->sock, http_response, http_response_length, 0);
	data->shouldfree = 1;

	free(failure_reason);
	free(http_response);
	return;

	error:
		if(failure_reason) free(failure_reason);
		if(http_response) free(http_response);
		return;
}
