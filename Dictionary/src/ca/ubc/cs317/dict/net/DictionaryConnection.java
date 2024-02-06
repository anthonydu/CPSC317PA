package ca.ubc.cs317.dict.net;

import ca.ubc.cs317.dict.model.Database;
import ca.ubc.cs317.dict.model.Definition;
import ca.ubc.cs317.dict.model.MatchingStrategy;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.io.PrintWriter;
import java.net.Socket;
import java.util.*;
import java.util.function.Function;

/**
 * Created by Jonatan on 2017-09-09.
 */
public class DictionaryConnection {

    private static final int DEFAULT_PORT = 2628;
    private Socket socket;
    private PrintWriter out;
    private BufferedReader in;

    // https://stackoverflow.com/questions/25477562/split-java-string-by-space-and-not-by-double-quotation-that-includes-space
    private static String[] splitBySpaceExceptQuoted(String str) {
        return str.split("\"?( |$)(?=(([^\"]*\"){2})*[^\"]*$)\"?");
    }

    private void readLines(Function<String, Void> func, String code) throws Exception {
        String lineString = in.readLine();
        System.out.println(lineString);
        String[] line = splitBySpaceExceptQuoted(lineString);
        if (!line[0].equals(code)) {
            throw new Exception(lineString);
        }
        readLines(func);
    }

    private String readLines(Function<String, Void> func) throws Exception {
        while (true) {
            String lineString = in.readLine();
            System.out.println(lineString);
            if (lineString.equals(".")) {
                break;
            } else {
                func.apply(lineString);
            }
        }
        String nextLine = in.readLine();
        System.out.println(nextLine);
        return nextLine;
    }

    /**
     * Establishes a new connection with a DICT server using an explicit host and
     * port number, and handles initial
     * welcome messages.
     *
     * @param host Name of the host where the DICT server is running
     * @param port Port number used by the DICT server
     * @throws DictConnectionException If the host does not exist, the connection
     *                                 can't be established, or the messages
     *                                 don't match their expected value.
     */
    public DictionaryConnection(String host, int port) throws DictConnectionException {
        try {
            socket = new Socket(host, port);
            out = new PrintWriter(socket.getOutputStream(), true);
            in = new BufferedReader(new InputStreamReader(socket.getInputStream()));
            String lineString = in.readLine();
            System.out.println(lineString);
            if (!lineString.startsWith("220")) {
                throw new Exception(lineString);
            }
        } catch (Exception e) {
            throw new DictConnectionException(e.getMessage());
        }
    }

    /**
     * Establishes a new connection with a DICT server using an explicit host, with
     * the default DICT port number, and
     * handles initial welcome messages.
     *
     * @param host Name of the host where the DICT server is running
     * @throws DictConnectionException If the host does not exist, the connection
     *                                 can't be established, or the messages
     *                                 don't match their expected value.
     */
    public DictionaryConnection(String host) throws DictConnectionException {
        this(host, DEFAULT_PORT);
    }

    /**
     * Sends the final QUIT message and closes the connection with the server. This
     * function ignores any exception that
     * may happen while sending the message, receiving its reply, or closing the
     * connection.
     *
     */
    public synchronized void close() {
        try {
            out.println("quit");
            System.out.println(in.readLine());
            in.close();
            out.close();
            socket.close();
        } catch (Exception e) {
            // do nothing
        }
    }

    /**
     * Requests and retrieves all definitions for a specific word.
     *
     * @param word     The word whose definition is to be retrieved.
     * @param database The database to be used to retrieve the definition. A special
     *                 database may be specified,
     *                 indicating either that all regular databases should be used
     *                 (database name '*'), or that only
     *                 definitions in the first database that has a definition for
     *                 the word should be used
     *                 (database '!').
     * @return A collection of Definition objects containing all definitions
     *         returned by the server.
     * @throws DictConnectionException If the connection was interrupted or the
     *                                 messages don't match their expected value.
     */
    public synchronized Collection<Definition> getDefinitions(String word, Database database)
            throws DictConnectionException {
        Collection<Definition> set = new ArrayList<>();
        try {
            out.println("define " + database.getName() + " \"" + word + "\"");
            String lineString = in.readLine();
            System.out.println(lineString);
            String[] line = splitBySpaceExceptQuoted(lineString);
            if (line[0].equals("150")) {
                int numDefs = Integer.parseInt(line[1]);
                String nextLine = in.readLine();
                System.out.println(nextLine);
                for (int i = 0; i < numDefs; i++) {
                    line = splitBySpaceExceptQuoted(nextLine);
                    if (line[0].equals("250")) {
                        break;
                    }
                    if (!line[0].equals("151")) {
                        throw new Exception(lineString);
                    }
                    Definition def = new Definition(line[1], line[2]);
                    set.add(def);
                    StringBuilder sb = new StringBuilder();
                    nextLine = readLines(ls -> {
                        sb.append(ls + "\n");
                        return null;
                    });
                    def.setDefinition(sb.toString().trim());
                }
            } else if (lineString.startsWith("55")) {
                return set;
            } else {
                throw new Exception(lineString);
            }
        } catch (Exception e) {
            throw new DictConnectionException(e.getMessage());
        }
        return set;
    }

    /**
     * Requests and retrieves a list of matches for a specific word pattern.
     *
     * @param word     The word whose definition is to be retrieved.
     * @param strategy The strategy to be used to retrieve the list of matches
     *                 (e.g., prefix, exact).
     * @param database The database to be used to retrieve the definition. A special
     *                 database may be specified,
     *                 indicating either that all regular databases should be used
     *                 (database name '*'), or that only
     *                 matches in the first database that has a match for the word
     *                 should be used (database '!').
     * @return A set of word matches returned by the server.
     * @throws DictConnectionException If the connection was interrupted or the
     *                                 messages don't match their expected value.
     */
    public synchronized Set<String> getMatchList(String word, MatchingStrategy strategy, Database database)
            throws DictConnectionException {
        Set<String> set = new LinkedHashSet<>();
        out.println("match " + database.getName() + " " + strategy.getName() + " \"" + word + "\"");
        try {
            readLines(ls -> {
                String[] l = splitBySpaceExceptQuoted(ls);
                set.add(l[1]);
                return null;
            }, "152");
        } catch (Exception e) {
            if (e.getMessage().startsWith("55")) {
                return set;
            }
            throw new DictConnectionException(e.getMessage());
        }
        return set;
    }

    /**
     * Requests and retrieves a map of database name to an equivalent database
     * object for all valid databases used in the server.
     *
     * @return A map of Database objects supported by the server.
     * @throws DictConnectionException If the connection was interrupted or the
     *                                 messages don't match their expected value.
     */
    public synchronized Map<String, Database> getDatabaseList() throws DictConnectionException {
        Map<String, Database> databaseMap = new HashMap<>();
        out.println("show db");
        try {
            readLines(ls -> {
                String[] l = splitBySpaceExceptQuoted(ls);
                databaseMap.put(l[0], new Database(l[0], l[1]));
                return null;
            }, "110");
        } catch (Exception e) {
            if (e.getMessage().startsWith("55")) {
                return databaseMap;
            }
            throw new DictConnectionException(e.getMessage());
        }
        return databaseMap;
    }

    /**
     * Requests and retrieves a list of all valid matching strategies supported by
     * the server.
     *
     * @return A set of MatchingStrategy objects supported by the server.
     * @throws DictConnectionException If the connection was interrupted or the
     *                                 messages don't match their expected value.
     */
    public synchronized Set<MatchingStrategy> getStrategyList() throws DictConnectionException {
        Set<MatchingStrategy> set = new LinkedHashSet<>();
        out.println("show strat");
        try {
            readLines(ls -> {
                String[] l = splitBySpaceExceptQuoted(ls);
                set.add(new MatchingStrategy(l[0], l[1]));
                return null;
            }, "111");
        } catch (Exception e) {
            if (e.getMessage().startsWith("55")) {
                return set;
            }
            throw new DictConnectionException(e.getMessage());
        }
        return set;
    }

    /**
     * Requests and retrieves detailed information about the currently selected
     * database.
     *
     * @return A string containing the information returned by the server in
     *         response to a "SHOW INFO <db>" command.
     * @throws DictConnectionException If the connection was interrupted or the
     *                                 messages don't match their expected value.
     */
    public synchronized String getDatabaseInfo(Database d) throws DictConnectionException {
        if (d.getName().equals("*") || d.getName().equals("!")) {
            return "";
        }
        StringBuilder sb = new StringBuilder();
        out.println("show info " + d.getName());
        try {
            readLines(ls -> {
                sb.append(ls + "\n");
                return null;
            }, "112");
        } catch (Exception e) {
            throw new DictConnectionException(e.getMessage());
        }
        return sb.toString().trim();
    }
}
