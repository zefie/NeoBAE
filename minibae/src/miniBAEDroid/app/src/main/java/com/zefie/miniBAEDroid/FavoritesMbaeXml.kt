package com.zefie.miniBAEDroid

import android.util.Xml
import org.xmlpull.v1.XmlPullParser
import java.io.InputStream
import java.io.OutputStream
import java.io.BufferedInputStream
import java.util.zip.GZIPInputStream
import java.util.zip.GZIPOutputStream

object FavoritesMbaeXml {
    private const val ROOT = "mbaeFavorites"
    private const val VERSION = "1"

    fun writeTo(output: OutputStream, favorites: List<String>) {
        val serializer = Xml.newSerializer()
        serializer.setOutput(output, "UTF-8")
        serializer.startDocument("UTF-8", true)
        serializer.startTag(null, ROOT)
        serializer.attribute(null, "version", VERSION)

        favorites.forEach { path ->
            serializer.startTag(null, "favorite")
            serializer.attribute(null, "path", path)
            serializer.endTag(null, "favorite")
        }

        serializer.endTag(null, ROOT)
        serializer.endDocument()
        serializer.flush()
    }

    fun writeCompressedTo(output: OutputStream, favorites: List<String>) {
        // GZIP the XML payload, but keep the .mbae extension.
        GZIPOutputStream(output).use { gz ->
            writeTo(gz, favorites)
        }
    }

    fun readFrom(input: InputStream): List<String> {
        // Auto-detect gzip vs raw XML for backward compatibility.
        val buffered = if (input.markSupported()) input else BufferedInputStream(input)
        buffered.mark(2)
        val b1 = buffered.read()
        val b2 = buffered.read()
        buffered.reset()

        return if (b1 == 0x1f && b2 == 0x8b) {
            GZIPInputStream(buffered).use { gz ->
                readXmlFrom(gz)
            }
        } else {
            readXmlFrom(buffered)
        }
    }

    private fun readXmlFrom(input: InputStream): List<String> {
        val parser = Xml.newPullParser()
        parser.setFeature(XmlPullParser.FEATURE_PROCESS_NAMESPACES, false)
        parser.setInput(input, "UTF-8")

        val favorites = ArrayList<String>()
        val seen = HashSet<String>()

        var eventType = parser.eventType
        while (eventType != XmlPullParser.END_DOCUMENT) {
            if (eventType == XmlPullParser.START_TAG) {
                when (parser.name) {
                    "favorite" -> {
                        val pathAttr = parser.getAttributeValue(null, "path")
                        val pathText = pathAttr ?: readText(parser)
                        val path = pathText?.trim().orEmpty()
                        if (path.isNotEmpty() && seen.add(path)) {
                            favorites.add(path)
                        }
                    }
                }
            }
            eventType = parser.next()
        }

        return favorites
    }

    private fun readText(parser: XmlPullParser): String? {
        // If the tag has text content, read it; otherwise return null.
        return if (parser.next() == XmlPullParser.TEXT) {
            val text = parser.text
            // Move parser to END_TAG
            parser.nextTag()
            text
        } else {
            null
        }
    }
}
