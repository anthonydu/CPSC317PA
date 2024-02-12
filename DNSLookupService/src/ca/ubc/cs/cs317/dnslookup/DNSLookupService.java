package ca.ubc.cs.cs317.dnslookup;

import java.io.*;
import java.net.*;
import java.util.*;

public class DNSLookupService {

    public static final int DEFAULT_DNS_PORT = 53;
    private static final int MAX_INDIRECTION_LEVEL_NS = 10;
    private static final int MAX_QUERY_ATTEMPTS = 3;
    private static final int MAX_DNS_MESSAGE_LENGTH = 512;
    private static final int MAX_EDNS_MESSAGE_LENGTH = 1280;
    private static final int SO_TIMEOUT = 5000;

    private final DNSCache cache = DNSCache.getInstance();
    private final Random random = new Random();
    private final DNSVerbosePrinter verbose;
    private final DatagramSocket socket;

    /**
     * Creates a new lookup service. Also initializes the datagram socket object
     * with a default timeout.
     *
     * @param verbose A DNSVerbosePrinter listener object with methods to be called
     *                at key events in the query
     *                processing.
     * @throws SocketException      If a DatagramSocket cannot be created.
     * @throws UnknownHostException If the nameserver is not a valid server.
     */
    public DNSLookupService(DNSVerbosePrinter verbose) throws SocketException, UnknownHostException {
        this.verbose = verbose;
        socket = new DatagramSocket();
        socket.setSoTimeout(SO_TIMEOUT);
    }

    /**
     * Closes the lookup service and related sockets and resources.
     */
    public void close() {
        socket.close();
    }

    /**
     * Examines a set of resource records to see if any of them are an answer to the
     * given question.
     *
     * @param rrs      The set of resource records to be examined
     * @param question The DNS question
     * @return true if the collection of resource records contains an answer to the
     *         given question.
     */
    private boolean containsAnswer(Collection<CommonResourceRecord> rrs, DNSQuestion question) {
        for (CommonResourceRecord rr : rrs) {
            if (rr.getQuestion().equals(question) && rr.getRecordType() == question.getRecordType()) {
                return true;
            }
        }
        return false;
    }

    /**
     * Finds all the results for a specific question. If there are valid (not
     * expired) results in the cache, uses these
     * results, otherwise queries the nameserver for new records. If there are CNAME
     * records associated to the question,
     * they are retrieved recursively for new records of the same type, and the
     * returning set will contain both the
     * CNAME record and the resulting resource records of the indicated type.
     *
     * @param question             Host and record type to be used for search.
     * @param maxIndirectionLevels Number of CNAME indirection levels to support.
     * @return A set of resource records corresponding to the specific query
     *         requested.
     * @throws DNSErrorException If the number CNAME redirection levels exceeds the
     *                           value set in
     *                           maxIndirectionLevels.
     */
    public Collection<CommonResourceRecord> getResultsFollowingCNames(DNSQuestion question, int maxIndirectionLevels)
            throws DNSErrorException {

        if (maxIndirectionLevels < 0)
            throw new DNSErrorException("CNAME indirection limit exceeded");

        Collection<CommonResourceRecord> directResults = iterativeQuery(question);
        if (containsAnswer(directResults, question)) {
            return directResults;
        }

        Set<CommonResourceRecord> newResults = new HashSet<>();
        for (CommonResourceRecord record : directResults) {
            newResults.add(record);
            if (record.getRecordType() == RecordType.CNAME) {
                newResults.addAll(getResultsFollowingCNames(
                        new DNSQuestion(record.getTextResult(), question.getRecordType(), question.getRecordClass()),
                        maxIndirectionLevels - 1));
            }
        }
        return newResults;
    }

    /**
     * Answers one question. If there are valid (not expired) results in the cache,
     * returns these results.
     * Otherwise it chooses the best nameserver to query, retrieves results from
     * that server
     * (using individualQueryProcess which adds all the results to the cache) and
     * repeats until either:
     * the cache contains an answer to the query, or
     * the cache contains an answer to the query that is a CNAME record rather than
     * the requested type, or
     * every "best" nameserver in the cache has already been tried.
     *
     * @param question Host name and record type/class to be used for the query.
     */
    public Collection<CommonResourceRecord> iterativeQuery(DNSQuestion question) {
        /* TO/DO: To be implemented by the student */
        for (int i = 0; i < MAX_INDIRECTION_LEVEL_NS; i++) {
            Collection<CommonResourceRecord> cachedResults = cache.getCachedResults(question);
            Collection<CommonResourceRecord> bestNameservers = cache.getBestNameservers(question);
            Collection<CommonResourceRecord> bestKnownNameservers = cache.filterByKnownIPAddress(bestNameservers);
            if (containsAnswer(cachedResults, question)) {
                return cachedResults;
            }
            for (CommonResourceRecord cachedResult : cachedResults) {
                if (cachedResult.getRecordType() == RecordType.CNAME) {
                    return cachedResults;
                }
            }
            if (bestKnownNameservers.isEmpty())
                for (CommonResourceRecord bestNameserver : bestNameservers)
                    try {
                        getResultsFollowingCNames(DNSCache.AQuestion(bestNameserver.getTextResult()),
                                MAX_INDIRECTION_LEVEL_NS);
                        break;
                    } catch (DNSErrorException e) {
                        continue;
                    }
            else
                for (CommonResourceRecord bestKnownNameserver : bestKnownNameservers)
                    try {
                        individualQueryProcess(question, bestKnownNameserver.getInetResult());
                        break;
                    } catch (DNSErrorException e) {
                        continue;
                    }
        }
        return cache.getCachedResults(question);
    }

    /**
     * Handles the process of sending an individual DNS query with a single
     * question. Builds and sends the query (request)
     * message, then receives and parses the response. Received responses that do
     * not match the requested transaction ID
     * are ignored. If no response is received after SO_TIMEOUT milliseconds, the
     * request is sent again, with the same
     * transaction ID. The query should be sent at most MAX_QUERY_ATTEMPTS times,
     * after which the function should return
     * without changing any values. If a response is received, all of its records
     * are added to the cache.
     * <p>
     * If the reply contains a non-zero Rcode value, then throw a DNSErrorException.
     * <p>
     * The method verbose.printQueryToSend() must be called every time a new query
     * message is about to be sent.
     *
     * @param question Host name and record type/class to be used for the query.
     * @param server   Address of the server to be used for the query.
     * @return If no response is received, returns null. Otherwise, returns a set of
     *         all resource records
     *         received in the response.
     * @throws DNSErrorException if the Rcode in the response is non-zero
     */
    public Set<ResourceRecord> individualQueryProcess(DNSQuestion question, InetAddress server)
            throws DNSErrorException {
        /* TO/DO: To be implemented by the student */
        DNSMessage message = buildQuery(question);
        verbose.printQueryToSend("UDP", question, server, message.getID());
        int messageLength = MAX_DNS_MESSAGE_LENGTH;
        for (int i = 0; i < MAX_QUERY_ATTEMPTS; i++) {
            try {
                socket.send(new DatagramPacket(message.getUsed(), message.getUsed().length, server, DEFAULT_DNS_PORT));
                byte[] buffer = new byte[messageLength];
                socket.receive(new DatagramPacket(buffer, messageLength));
                DNSMessage response = new DNSMessage(buffer, buffer.length);
                Set<ResourceRecord> ans = processResponse(response);
                if (!response.getQR())
                    continue;
                if (response.getTC() && messageLength != MAX_EDNS_MESSAGE_LENGTH) {
                    messageLength = MAX_EDNS_MESSAGE_LENGTH;
                    OPTResourceRecord opt = new OPTResourceRecord(messageLength, 0, new byte[0], question);
                    message.addResourceRecord(opt, "additional");
                } else if (response.getID() == message.getID()) {
                    return ans;
                }
            } catch (SocketException e) {
                e.printStackTrace();
            } catch (SocketTimeoutException e) {
                continue;
            } catch (IOException e) {
                e.printStackTrace();
            }
        }
        return null;
    }

    /**
     * Creates a DNSMessage containing a DNS query.
     * A random transaction ID must be generated and filled in the corresponding
     * part of the query. The query
     * must be built as an iterative (non-recursive) request for a regular query
     * with a single question. When the
     * function returns, the message's buffer's position (`message.buffer.position`)
     * must be equivalent
     * to the size of the query data.
     *
     * @param question Host name and record type/class to be used for the query.
     * @return The DNSMessage containing the query.
     */
    public DNSMessage buildQuery(DNSQuestion question) {
        /* TO/DO: To be implemented by the student */
        DNSMessage message = new DNSMessage((short) random.nextInt());
        message.addQuestion(question);
        message.setQDCount(1);
        return message;
    }

    /**
     * Parses and processes a response received by a nameserver.
     * If the reply contains a non-zero Rcode value, then throw a DNSErrorException.
     * Adds all resource records found in the response message to the cache.
     * Calls methods in the verbose object at appropriate points of the processing
     * sequence. Must be able
     * to properly parse records of the types: A, AAAA, NS, CNAME and MX (the
     * priority field for MX may be ignored). Any
     * other unsupported record type must create a record object with the data
     * represented as a hex string (see method
     * byteArrayToHexString).
     *
     * @param message The DNSMessage received from the server.
     * @return A set of all resource records received in the response.
     * @throws DNSErrorException if the Rcode value in the reply header is non-zero
     */
    public Set<ResourceRecord> processResponse(DNSMessage message) throws DNSErrorException {
        /* TO/DO: To be implemented by the student */
        if (message.getRcode() != 0)
            throw new DNSErrorException("RCODE is " + message.getRcode());
        message.getQuestion();
        Set<ResourceRecord> rrs = new HashSet<>();
        int num_answers = message.getANCount();
        int num_nameservers = message.getNSCount();
        int num_additional = message.getARCount();
        verbose.printResponseHeaderInfo(message.getID(), message.getAA(), message.getTC(), message.getRcode());
        verbose.printAnswersHeader(num_answers);
        for (int i = 0; i < num_answers; i++) {
            ResourceRecord rr = message.getRR();
            rrs.add(rr);
            cache.addResult((CommonResourceRecord) rr);
            verbose.printIndividualResourceRecord(rr, rr.getRecordType().getCode(), rr.getRecordClassCode());
        }
        verbose.printNameserversHeader(num_nameservers);
        for (int i = 0; i < num_nameservers; i++) {
            ResourceRecord rr = message.getRR();
            rrs.add(rr);
            cache.addResult((CommonResourceRecord) rr);
            verbose.printIndividualResourceRecord(rr, rr.getRecordType().getCode(), rr.getRecordClassCode());
        }
        verbose.printAdditionalInfoHeader(num_additional);
        for (int i = 0; i < num_additional; i++) {
            ResourceRecord rr = message.getRR();
            if (rr.getRecordType() != RecordType.OPT) {
                rrs.add(rr);
                cache.addResult((CommonResourceRecord) rr);
            }
            verbose.printIndividualResourceRecord(rr, rr.getRecordType().getCode(), rr.getRecordClassCode());
        }
        return rrs;
    }

    public static class DNSErrorException extends Exception {
        public DNSErrorException(String msg) {
            super(msg);
        }
    }
}
