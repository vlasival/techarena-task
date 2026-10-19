// Minimal working C++ submission for TechArena Solver (model 20261).
// Office 0, one site, greedy workers for that site's tasks.

#include <algorithm>
#include <cctype>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

std::string read_file(const char* path) {
	std::ifstream in(path, std::ios::binary);
	if (!in) {
		throw std::runtime_error("cannot open input");
	}
	return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void skip_ws(const std::string& s, std::size_t& i) {
	while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) {
		++i;
	}
}

struct Json {
	enum class Type { Null, Bool, Number, String, Array, Object };
	Type type = Type::Null;
	double number = 0;
	std::string str;
	std::vector<Json> array;
	std::vector<std::pair<std::string, Json>> object;

	static Json parse(const std::string& s, std::size_t& i);

	static Json parse_value(const std::string& s, std::size_t& i) {
		skip_ws(s, i);
		if (i >= s.size()) {
			throw std::runtime_error("unexpected end");
		}
		const char c = s[i];
		if (c == '{') {
			return parse_object(s, i);
		}
		if (c == '[') {
			return parse_array(s, i);
		}
		if (c == '"') {
			return parse_string(s, i);
		}
		if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
			return parse_number(s, i);
		}
		throw std::runtime_error("bad json");
	}

	static Json parse_object(const std::string& s, std::size_t& i) {
		Json out;
		out.type = Type::Object;
		++i;
		skip_ws(s, i);
		if (i < s.size() && s[i] == '}') {
			++i;
			return out;
		}
		while (true) {
			Json key = parse_string(s, i);
			skip_ws(s, i);
			if (i >= s.size() || s[i] != ':') {
				throw std::runtime_error("expected :");
			}
			++i;
			out.object.emplace_back(key.str, parse_value(s, i));
			skip_ws(s, i);
			if (i >= s.size()) {
				throw std::runtime_error("unclosed object");
			}
			if (s[i] == '}') {
				++i;
				return out;
			}
			if (s[i] != ',') {
				throw std::runtime_error("expected ,");
			}
			++i;
		}
	}

	static Json parse_array(const std::string& s, std::size_t& i) {
		Json out;
		out.type = Type::Array;
		++i;
		skip_ws(s, i);
		if (i < s.size() && s[i] == ']') {
			++i;
			return out;
		}
		while (true) {
			out.array.push_back(parse_value(s, i));
			skip_ws(s, i);
			if (i >= s.size()) {
				throw std::runtime_error("unclosed array");
			}
			if (s[i] == ']') {
				++i;
				return out;
			}
			if (s[i] != ',') {
				throw std::runtime_error("expected ,");
			}
			++i;
		}
	}

	static Json parse_string(const std::string& s, std::size_t& i) {
		Json out;
		out.type = Type::String;
		if (s[i] != '"') {
			throw std::runtime_error("expected string");
		}
		++i;
		while (i < s.size()) {
			const char c = s[i++];
			if (c == '"') {
				return out;
			}
			if (c == '\\' && i < s.size()) {
				out.str.push_back(s[i++]);
			} else {
				out.str.push_back(c);
			}
		}
		throw std::runtime_error("unterminated string");
	}

	static Json parse_number(const std::string& s, std::size_t& i) {
		Json out;
		out.type = Type::Number;
		const std::size_t start = i;
		if (s[i] == '-') {
			++i;
		}
		while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.' || s[i] == 'e' ||
		                        s[i] == 'E' || s[i] == '+' || s[i] == '-')) {
			++i;
		}
		out.number = std::stod(s.substr(start, i - start));
		return out;
	}

	const Json* find(const std::string& key) const {
		if (type != Type::Object) {
			return nullptr;
		}
		for (const auto& entry : object) {
			if (entry.first == key) {
				return &entry.second;
			}
		}
		return nullptr;
	}
};

Json Json::parse(const std::string& s, std::size_t& i) {
	return parse_value(s, i);
}

std::vector<int> as_int_list(const Json& value) {
	std::vector<int> out;
	if (value.type != Json::Type::Array) {
		return out;
	}
	for (const Json& item : value.array) {
		out.push_back(static_cast<int>(item.number));
	}
	return out;
}

std::optional<std::vector<int>> cover_tasks(const std::vector<std::vector<int>>& workers,
                                           const std::vector<int>& site_tasks) {
	std::unordered_set<int> need(site_tasks.begin(), site_tasks.end());
	if (need.empty()) {
		return std::vector<int>{};
	}
	std::vector<int> chosen;
	std::unordered_set<int> used;
	while (!need.empty()) {
		int found = -1;
		for (int wid = 0; wid < static_cast<int>(workers.size()); ++wid) {
			if (used.count(wid) != 0) {
				continue;
			}
			bool covers_any = false;
			for (int task : need) {
				if (std::find(workers[wid].begin(), workers[wid].end(), task) != workers[wid].end()) {
					covers_any = true;
					break;
				}
			}
			if (covers_any) {
				found = wid;
				break;
			}
		}
		if (found < 0) {
			return std::nullopt;
		}
		chosen.push_back(found);
		used.insert(found);
		for (int skill : workers[found]) {
			need.erase(skill);
		}
	}
	std::sort(chosen.begin(), chosen.end());
	return chosen;
}

std::string minimal_solution(const Json& root) {
	const Json* sites = root.find("sites");
	const Json* offices = root.find("offices");
	const Json* workers_node = root.find("workers");
	if (sites == nullptr || offices == nullptr || workers_node == nullptr ||
	    sites->type != Json::Type::Array || offices->type != Json::Type::Array ||
	    workers_node->type != Json::Type::Array || sites->array.empty() || offices->array.empty()) {
		return "[]\n";
	}
	const Json& office0 = offices->array.front();
	const Json* max_size = office0.find("maxSize");
	if (max_size == nullptr || max_size->number < 1) {
		return "[]\n";
	}
	std::vector<std::vector<int>> workers;
	for (const Json& row : workers_node->array) {
		workers.push_back(as_int_list(row));
	}
	for (int site_id = 0; site_id < static_cast<int>(sites->array.size()); ++site_id) {
		const Json& site = sites->array[static_cast<std::size_t>(site_id)];
		const Json* tasks_node = site.find("tasks");
		std::vector<int> tasks = tasks_node != nullptr ? as_int_list(*tasks_node) : std::vector<int>{};
		const auto picked = cover_tasks(workers, tasks);
		if (!picked.has_value()) {
			continue;
		}
		std::ostringstream out;
		out << "[\n\t{\n\t\t\"officeId\": 0,\n\t\t\"sites\": [" << site_id << "],\n\t\t\"workers\": [";
		for (std::size_t j = 0; j < picked->size(); ++j) {
			if (j != 0) {
				out << ", ";
			}
			out << (*picked)[j];
		}
		out << "]\n\t}\n]\n";
		return out.str();
	}
	return "[]\n";
}

}  // namespace

int main(int argc, char** argv) {
	try {
		const char* input_path = argc > 1 ? argv[1] : "instance.json";
		const char* output_path = argc > 2 ? argv[2] : "solution.json";
		std::string text = read_file(input_path);
		if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
		    static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) {
			text.erase(0, 3);
		}
		std::size_t pos = 0;
		const Json root = Json::parse(text, pos);
		std::ofstream output(output_path, std::ios::binary);
		if (!output) {
			return 1;
		}
		output << minimal_solution(root);
		return output.good() ? 0 : 1;
	} catch (...) {
		return 1;
	}
}
