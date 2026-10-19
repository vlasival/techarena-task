import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.TreeSet;

/** Minimal working Java submission for TechArena Solver (model 20261). */
public class Main {
    static final class Json {
        static Object parse(String text) {
            return new Parser(text).parseValue();
        }

        static final class Parser {
            final String s;
            int i = 0;

            Parser(String s) {
                this.s = s;
            }

            void skipWs() {
                while (i < s.length() && Character.isWhitespace(s.charAt(i))) {
                    i++;
                }
            }

            char peek() {
                skipWs();
                return i < s.length() ? s.charAt(i) : 0;
            }

            Object parseValue() {
                skipWs();
                char c = peek();
                if (c == '{') {
                    return parseObject();
                }
                if (c == '[') {
                    return parseArray();
                }
                if (c == '"') {
                    return parseString();
                }
                if (c == '-' || Character.isDigit(c)) {
                    return parseNumber();
                }
                throw new RuntimeException("bad json at " + i);
            }

            String parseString() {
                expect('"');
                StringBuilder out = new StringBuilder();
                while (i < s.length()) {
                    char c = s.charAt(i++);
                    if (c == '"') {
                        return out.toString();
                    }
                    if (c == '\\') {
                        out.append(s.charAt(i++));
                    } else {
                        out.append(c);
                    }
                }
                throw new RuntimeException("unterminated string");
            }

            Number parseNumber() {
                int start = i;
                if (s.charAt(i) == '-') {
                    i++;
                }
                while (i < s.length() && (Character.isDigit(s.charAt(i)) || s.charAt(i) == '.' ||
                        s.charAt(i) == 'e' || s.charAt(i) == 'E' || s.charAt(i) == '+' || s.charAt(i) == '-')) {
                    i++;
                }
                String token = s.substring(start, i);
                if (token.contains(".") || token.contains("e") || token.contains("E")) {
                    return Double.parseDouble(token);
                }
                return Integer.parseInt(token);
            }

            @SuppressWarnings("unchecked")
            java.util.Map<String, Object> parseObject() {
                expect('{');
                java.util.Map<String, Object> map = new java.util.LinkedHashMap<>();
                skipWs();
                if (peek() == '}') {
                    i++;
                    return map;
                }
                while (true) {
                    String key = parseString();
                    skipWs();
                    expect(':');
                    map.put(key, parseValue());
                    skipWs();
                    char c = s.charAt(i++);
                    if (c == '}') {
                        return map;
                    }
                    if (c != ',') {
                        throw new RuntimeException("expected , or }");
                    }
                }
            }

            List<Object> parseArray() {
                expect('[');
                List<Object> list = new ArrayList<>();
                skipWs();
                if (peek() == ']') {
                    i++;
                    return list;
                }
                while (true) {
                    list.add(parseValue());
                    skipWs();
                    char c = s.charAt(i++);
                    if (c == ']') {
                        return list;
                    }
                    if (c != ',') {
                        throw new RuntimeException("expected , or ]");
                    }
                }
            }

            void expect(char ch) {
                skipWs();
                if (i >= s.length() || s.charAt(i) != ch) {
                    throw new RuntimeException("expected " + ch);
                }
                i++;
            }
        }
    }

    @SuppressWarnings("unchecked")
    static List<Integer> intList(Object value) {
        List<Integer> out = new ArrayList<>();
        if (!(value instanceof List)) {
            return out;
        }
        for (Object item : (List<Object>) value) {
            out.add(((Number) item).intValue());
        }
        return out;
    }

    static List<Integer> coverTasks(List<List<Integer>> workers, List<Integer> siteTasks) {
        Set<Integer> need = new HashSet<>(siteTasks);
        if (need.isEmpty()) {
            return List.of();
        }
        List<Integer> chosen = new ArrayList<>();
        Set<Integer> used = new HashSet<>();
        while (!need.isEmpty()) {
            int found = -1;
            for (int wid = 0; wid < workers.size(); wid++) {
                if (used.contains(wid)) {
                    continue;
                }
                Set<Integer> skills = new HashSet<>(workers.get(wid));
                boolean coversAny = false;
                for (int task : need) {
                    if (skills.contains(task)) {
                        coversAny = true;
                        break;
                    }
                }
                if (coversAny) {
                    found = wid;
                    break;
                }
            }
            if (found < 0) {
                return null;
            }
            chosen.add(found);
            used.add(found);
            need.removeAll(new HashSet<>(workers.get(found)));
        }
        return new ArrayList<>(new TreeSet<>(chosen));
    }

    @SuppressWarnings("unchecked")
    static String minimalSolution(java.util.Map<String, Object> instance) {
        List<Object> sites = (List<Object>) instance.get("sites");
        List<Object> offices = (List<Object>) instance.get("offices");
        List<Object> workersRaw = (List<Object>) instance.get("workers");
        if (sites == null || sites.isEmpty() || offices == null || offices.isEmpty()) {
            return "[]\n";
        }
        java.util.Map<String, Object> office0 = (java.util.Map<String, Object>) offices.get(0);
        int maxSize = ((Number) office0.get("maxSize")).intValue();
        if (maxSize < 1) {
            return "[]\n";
        }
        List<List<Integer>> workers = new ArrayList<>();
        for (Object row : workersRaw) {
            workers.add(intList(row));
        }
        for (int siteId = 0; siteId < sites.size(); siteId++) {
            java.util.Map<String, Object> site = (java.util.Map<String, Object>) sites.get(siteId);
            List<Integer> tasks = intList(site.get("tasks"));
            List<Integer> picked = coverTasks(workers, tasks);
            if (picked == null) {
                continue;
            }
            StringBuilder workersJson = new StringBuilder();
            for (int j = 0; j < picked.size(); j++) {
                if (j > 0) {
                    workersJson.append(", ");
                }
                workersJson.append(picked.get(j));
            }
            return "[\n\t{\n\t\t\"officeId\": 0,\n\t\t\"sites\": [" + siteId + "],\n\t\t\"workers\": [" + workersJson
                    + "]\n\t}\n]\n";
        }
        return "[]\n";
    }

    public static void main(String[] args) throws Exception {
        Path input = Path.of(args.length > 0 ? args[0] : "instance.json");
        Path output = Path.of(args.length > 1 ? args[1] : "solution.json");
        String text = Files.readString(input, StandardCharsets.UTF_8);
        if (text.startsWith("\uFEFF")) {
            text = text.substring(1);
        }
        Object root = Json.parse(text);
        @SuppressWarnings("unchecked")
        java.util.Map<String, Object> instance = (java.util.Map<String, Object>) root;
        Files.writeString(output, minimalSolution(instance), StandardCharsets.UTF_8);
    }
}
